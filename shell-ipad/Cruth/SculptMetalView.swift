import MetalKit
import simd
import UIKit

/// The viewport. Input follows the PRD rule "Pencil makes geometry, fingers
/// move the camera" (PRD I-01, I-04):
///
/// - Apple Pencil: raw coalesced touches with pressure become stroke samples.
/// - One finger orbits, two fingers pan and pinch-zoom.
/// - Mouse or trackpad: click-drag orbits, two-finger scroll pans, pinch zooms.
/// - Two-finger tap undoes, three-finger tap redoes.
/// - Pencil double-tap toggles invert; Pencil Pro squeeze toggles Smooth.
/// - Hover (M2+ iPads, or a pointer) shows the brush footprint before contact.
///
/// In Retopo mode, Quad Pen and Insert Loop take taps, Slide Loop takes a drag
/// and the other tools take strokes.
///
/// With "finger sculpting" on, a single finger or a pointer click that lands on
/// the model sculpts; one that lands on the background still orbits. Pointer
/// clicks reach touchesBegan as .indirectPointer touches because Info.plist sets
/// UIApplicationSupportsIndirectInputEvents.
final class SculptMetalView: MTKView, UIGestureRecognizerDelegate, UIPencilInteractionDelegate {
    let session: SculptSession
    private(set) var renderer: Renderer?

    private let cursor = CAShapeLayer()
    private var orbitGesture: UIPanGestureRecognizer!
    private var panGesture: UIPanGestureRecognizer!
    private var pinchGesture: UIPinchGestureRecognizer!

    private var strokeTouch: UITouch?
    private var grabPlane: (point: SIMD3<Float>, normal: SIMD3<Float>)?

    // Guides and retopology draw as overlays, re-projected when the camera moves.
    // Parts hidden behind the model go to a faint dashed twin layer.
    private let guideLayer = CAShapeLayer()
    private let hiddenGuideLayer = CAShapeLayer()
    private let retopoLayer = CAShapeLayer()
    private let hiddenRetopoLayer = CAShapeLayer()
    private let pathLayer = CAShapeLayer()
    private let penLayer = CAShapeLayer()  // pending quad-pen taps
    private let hiddenPenLayer = CAShapeLayer()  // ones behind the model: hollow, faint, dashed
    private var pathPoints: [SIMD3<Float>] = []

    // Retopology tools that take taps or drags rather than strokes: the tool and
    // where the touch landed, whether it has moved too far to be a tap, and the
    // loop slide it started (axis, and the camera-facing plane it drags on).
    private var touchTool: RetopoTool?
    private var touchOrigin: (location: CGPoint, hit: SIMD3<Float>?)?
    private var touchTravelled = false
    private var slide: (axis: SIMD3<Float>, plane: (point: SIMD3<Float>, normal: SIMD3<Float>))?
    /// Screen distance, in points, a touch may move and still count as a tap.
    private static let tapSlop: CGFloat = 10
    /// Screen distance, in points, from the touch to the edge Insert Loop or
    /// Slide Loop would pick, beyond which the touch picks nothing.
    private static let edgePickRadius: CGFloat = 30

    init(session: SculptSession) {
        self.session = session
        super.init(frame: .zero, device: MTLCreateSystemDefaultDevice())
        colorPixelFormat = .bgra8Unorm_srgb
        depthStencilPixelFormat = .depth32Float
        clearColor = MTLClearColor(red: 0.11, green: 0.115, blue: 0.13, alpha: 1)
        preferredFramesPerSecond = 120
        isPaused = true
        enableSetNeedsDisplay = true
        isMultipleTouchEnabled = true

        renderer = Renderer(view: self, session: session)
        delegate = renderer

        installGestures()
        addInteraction(makePencilInteraction())

        for (overlay, color, width, hidden) in [(hiddenGuideLayer, UIColor.systemOrange, 2.0, true),
                                                (hiddenRetopoLayer, UIColor.systemTeal, 1.0, true),
                                                (guideLayer, UIColor.systemOrange, 3.0, false),
                                                (retopoLayer, UIColor.systemTeal, 1.5, false),
                                                (pathLayer, UIColor.systemYellow, 2.0, false)] {
            overlay.fillColor = UIColor.clear.cgColor
            overlay.strokeColor = color.withAlphaComponent(hidden ? 0.55 : 0.9).cgColor
            overlay.lineWidth = CGFloat(width)
            overlay.lineJoin = .round
            overlay.lineCap = hidden ? .butt : .round
            overlay.lineDashPattern = hidden ? [4, 4] : nil
            layer.addSublayer(overlay)
        }

        hiddenPenLayer.fillColor = UIColor.clear.cgColor
        hiddenPenLayer.strokeColor = UIColor.systemYellow.withAlphaComponent(0.55).cgColor
        hiddenPenLayer.lineWidth = 1.5
        hiddenPenLayer.lineDashPattern = [2, 2]
        layer.addSublayer(hiddenPenLayer)

        penLayer.fillColor = UIColor.systemYellow.cgColor
        penLayer.strokeColor = UIColor.black.withAlphaComponent(0.6).cgColor
        penLayer.lineWidth = 1
        layer.addSublayer(penLayer)

        cursor.fillColor = UIColor.clear.cgColor
        cursor.strokeColor = UIColor.white.withAlphaComponent(0.8).cgColor
        cursor.lineWidth = 1.5
        cursor.isHidden = true
        layer.addSublayer(cursor)
    }

    @available(*, unavailable)
    required init(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    /// Called by SwiftUI when the session publishes a change.
    func sessionDidChange() {
        refreshOverlay()
        setNeedsDisplay()
    }

    /// Projects guides and the retopology cage into the view, splitting each
    /// polyline into the parts in front of the model and the parts behind it.
    func refreshOverlay() {
        // While a background command owns the document, raycasts fail and the
        // viewport keeps its last frame, so keep the overlay that matches it.
        guard let renderer, !session.isBusy else { return }
        let camera = renderer.camera
        let size = bounds.size
        var tested: [SIMD3<Float>: Bool] = [:]  // cage edges share endpoints

        // A point is hidden when a ray from the eye meets the model clearly before
        // reaching it. Overlay points are snapped to the surface, so they hit
        // themselves; the tolerance scales with depth to absorb float error and
        // small drift after sculpting, without flicker.
        func isVisible(_ p: SIMD3<Float>) -> Bool {
            if let known = tested[p] { return known }
            let offset = p - camera.eye
            let depth = simd_length(offset)
            var visible = true
            if depth > 0, let hit = session.raycast(origin: camera.eye, direction: offset / depth) {
                visible = simd_distance(hit.position, camera.eye) >= depth * (1 - Self.occlusionTolerance)
            }
            tested[p] = visible
            return visible
        }

        func polyline(_ points: [SIMD3<Float>], shown: UIBezierPath, hidden: UIBezierPath) {
            var previous: (point: CGPoint, visible: Bool)?
            var lastTested = CGPoint.zero
            var current: UIBezierPath?
            for (i, p) in points.enumerated() {
                guard let q = camera.project(p, viewSize: size) else {
                    previous = nil
                    current = nil
                    continue
                }
                // Dense strokes are tested every few points on screen; points in
                // between take the state of the last tested one.
                let visible: Bool
                if let previous, i < points.count - 1,
                   hypot(q.x - lastTested.x, q.y - lastTested.y) < Self.occlusionSpacing {
                    visible = previous.visible
                } else {
                    visible = isVisible(p)
                    lastTested = q
                }
                if let previous {
                    let path = previous.visible && visible ? shown : hidden
                    if path !== current { path.move(to: previous.point) }
                    path.addLine(to: q)
                    current = path
                }
                previous = (q, visible)
            }
        }

        let guides = UIBezierPath(), hiddenGuides = UIBezierPath()
        for stroke in session.guideStrokes { polyline(stroke, shown: guides, hidden: hiddenGuides) }
        let cage = UIBezierPath(), hiddenCage = UIBezierPath()
        let edges = session.retopoEdges
        for i in stride(from: 0, to: edges.count - 1, by: 2) {
            polyline([edges[i], edges[i + 1]], shown: cage, hidden: hiddenCage)
        }
        let pen = UIBezierPath(), hiddenPen = UIBezierPath()
        for p in session.quadPenPending {
            guard let q = camera.project(p, viewSize: size) else { continue }
            (isVisible(p) ? pen : hiddenPen).append(UIBezierPath(ovalIn: CGRect(x: q.x - 5, y: q.y - 5,
                                                                                width: 10, height: 10)))
        }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        penLayer.path = pen.cgPath
        hiddenPenLayer.path = hiddenPen.cgPath
        guideLayer.path = guides.cgPath
        hiddenGuideLayer.path = hiddenGuides.cgPath
        retopoLayer.path = cage.cgPath
        hiddenRetopoLayer.path = hiddenCage.cgPath
        CATransaction.commit()
        refreshLivePath()
    }

    /// Fraction of a point's depth by which the model may lie in front of it
    /// before the point counts as hidden.
    private static let occlusionTolerance: Float = 0.005
    /// Screen distance, in points, between occlusion tests along a polyline.
    private static let occlusionSpacing: CGFloat = 4

    /// The stroke being drawn always lies on the visible surface, so it skips
    /// the occlusion test and can update on every touch cheaply.
    private func refreshLivePath() {
        guard let renderer else { return }
        let live = UIBezierPath()
        var started = false
        for p in pathPoints {
            guard let q = renderer.camera.project(p, viewSize: bounds.size) else {
                started = false
                continue
            }
            if started { live.addLine(to: q) } else { live.move(to: q) }
            started = true
        }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        pathLayer.path = live.cgPath
        CATransaction.commit()
    }

    // MARK: Gestures

    private func installGestures() {
        let direct = [NSNumber(value: UITouch.TouchType.direct.rawValue)]
        let directOrPointer = direct + [NSNumber(value: UITouch.TouchType.indirectPointer.rawValue)]

        orbitGesture = UIPanGestureRecognizer(target: self, action: #selector(handleOrbit(_:)))
        orbitGesture.minimumNumberOfTouches = 1
        orbitGesture.maximumNumberOfTouches = 1
        orbitGesture.allowedTouchTypes = directOrPointer

        panGesture = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        panGesture.minimumNumberOfTouches = 2
        panGesture.maximumNumberOfTouches = 2
        panGesture.allowedTouchTypes = direct
        panGesture.allowedScrollTypesMask = .continuous

        pinchGesture = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
        pinchGesture.allowedTouchTypes = directOrPointer

        let undoTap = UITapGestureRecognizer(target: self, action: #selector(handleUndo))
        undoTap.numberOfTouchesRequired = 2
        undoTap.allowedTouchTypes = direct

        let redoTap = UITapGestureRecognizer(target: self, action: #selector(handleRedo))
        redoTap.numberOfTouchesRequired = 3
        redoTap.allowedTouchTypes = direct

        let hover = UIHoverGestureRecognizer(target: self, action: #selector(handleHover(_:)))

        for g in [orbitGesture!, panGesture!, pinchGesture!, undoTap, redoTap, hover] as [UIGestureRecognizer] {
            g.delegate = self
            g.cancelsTouchesInView = false
            addGestureRecognizer(g)
        }
    }

    func gestureRecognizer(_ g: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer) -> Bool {
        // Pan and pinch run together for two-finger navigation.
        let navigation: [UIGestureRecognizer] = [panGesture, pinchGesture]
        return navigation.contains { $0 === g } && navigation.contains { $0 === other }
    }

    override func gestureRecognizerShouldBegin(_ g: UIGestureRecognizer) -> Bool {
        // Never navigate while a stroke is in progress.
        if g is UIHoverGestureRecognizer { return true }
        return strokeTouch == nil
    }

    @objc private func handleOrbit(_ g: UIPanGestureRecognizer) {
        let t = g.translation(in: self)
        g.setTranslation(.zero, in: self)
        defer { refreshOverlay() }
        renderer?.camera.orbit(dx: Float(t.x), dy: Float(t.y))
        setNeedsDisplay()
    }

    @objc private func handlePan(_ g: UIPanGestureRecognizer) {
        let t = g.translation(in: self)
        g.setTranslation(.zero, in: self)
        defer { refreshOverlay() }
        renderer?.camera.pan(dx: Float(t.x), dy: Float(t.y), viewHeight: Float(bounds.height))
        setNeedsDisplay()
    }

    @objc private func handlePinch(_ g: UIPinchGestureRecognizer) {
        defer { refreshOverlay() }
        renderer?.camera.zoom(by: Float(g.scale))
        g.scale = 1
        setNeedsDisplay()
    }

    @objc private func handleUndo() { session.undo() }
    @objc private func handleRedo() { session.redo() }

    @objc private func handleHover(_ g: UIHoverGestureRecognizer) {
        switch g.state {
        case .began, .changed:
            showCursor(at: g.location(in: self))
        default:
            cursor.isHidden = true
        }
    }

    // MARK: Pencil

    private func makePencilInteraction() -> UIPencilInteraction {
        let interaction = UIPencilInteraction()
        interaction.delegate = self
        return interaction
    }

    func pencilInteraction(_ interaction: UIPencilInteraction, didReceiveTap tap: UIPencilInteraction.Tap) {
        guard UIPencilInteraction.preferredTapAction != .ignore else { return }
        session.invert.toggle()
    }

    func pencilInteraction(_ interaction: UIPencilInteraction,
                           didReceiveSqueeze squeeze: UIPencilInteraction.Squeeze) {
        guard squeeze.phase == .ended, UIPencilInteraction.preferredSqueezeAction != .ignore else { return }
        session.toggleSmooth()
    }

    // MARK: Strokes (raw touches)

    private func sculpts(with touch: UITouch) -> Bool {
        guard !session.isBusy else { return false }  // a background command owns the document
        if touch.type == .pencil { return true }
        // A finger or pointer sculpts only when it lands on the model; on the
        // background it orbits, so finger sculpting still has a way to turn the view.
        let fingerLike = touch.type == .direct || touch.type == .indirectPointer
        return session.fingerSculpting && fingerLike && hitsModel(at: touch.location(in: self))
    }

    private func hitsModel(at point: CGPoint) -> Bool {
        guard let renderer else { return false }
        let ray = renderer.camera.ray(through: point, viewSize: bounds.size)
        return session.raycast(origin: ray.origin, direction: ray.direction) != nil
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesBegan(touches, with: event)
        if strokeTouch != nil, session.fingerSculpting, (event?.allTouches?.count ?? 0) > 1,
           strokeTouch?.type == .direct {
            // A second finger means navigation, not sculpting.
            finishStroke(commit: false)
            return
        }
        // Fingers that come down together (as a two-finger tap's can, in one
        // event) are a navigation or undo gesture, never a stroke or tool touch.
        let fingersDown = event?.allTouches?.filter {
            $0.type == .direct && $0.phase != .ended && $0.phase != .cancelled
        }.count ?? 0
        // Only the primary mouse button sculpts; fingers and Pencil carry no buttons.
        guard strokeTouch == nil, (event?.buttonMask ?? []).subtracting(.primary).isEmpty,
              let touch = touches.first(where: { sculpts(with: $0) && !($0.type == .direct && fingersDown > 1) })
        else { return }
        strokeTouch = touch
        grabPlane = nil
        pathPoints.removeAll()
        touchTool = session.mode == .retopo && session.retopoTool.input != .stroke ? session.retopoTool : nil
        touchTravelled = false
        slide = nil
        if session.mode == .sculpt { session.beginStroke() }
        if touchTool != nil {
            let location = touch.location(in: self)
            touchOrigin = (location, surfacePoint(at: location))
        } else {
            addSamples(for: touch, event: event)
        }
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesMoved(touches, with: event)
        guard let touch = strokeTouch, touches.contains(touch) else { return }
        if touchTool != nil { trackToolTouch(touch) } else { addSamples(for: touch, event: event) }
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesEnded(touches, with: event)
        guard let touch = strokeTouch, touches.contains(touch) else { return }
        if touchTool != nil { trackToolTouch(touch) } else { addSamples(for: touch, event: event) }
        finishStroke(commit: true)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesCancelled(touches, with: event)
        guard let touch = strokeTouch, touches.contains(touch) else { return }
        finishStroke(commit: false)
    }

    /// commit is false when the touch turned into navigation or was cancelled:
    /// a pending tap is then dropped rather than placed.
    private func finishStroke(commit: Bool) {
        if session.mode == .sculpt {
            session.endStroke()
        } else if let tool = touchTool {
            if slide != nil {
                session.endSlideLoop()
            } else if commit, !touchTravelled, let hit = touchOrigin?.hit {
                switch tool {
                case .quadPen: session.quadPenTap(at: hit)
                case .insertLoop:
                    if let origin = touchOrigin, edgeIsNear(hit, on: origin.location) {
                        session.insertLoop(near: hit)
                    } else {
                        session.flash("Tap closer to an edge")
                    }
                default: break
                }
            }
            touchTool = nil
            touchOrigin = nil
            slide = nil
            refreshOverlay()
        } else {
            // Guide or retopology stroke: hand the surface path to the session.
            let points = pathPoints
            pathPoints.removeAll()
            session.completePath(points, viewDirection: renderer?.camera.forward ?? SIMD3(0, 0, -1))
            refreshOverlay()
        }
        strokeTouch = nil
        grabPlane = nil
        cursor.isHidden = true
        setNeedsDisplay()
    }

    private func surfacePoint(at point: CGPoint) -> SIMD3<Float>? {
        guard let renderer else { return nil }
        let ray = renderer.camera.ray(through: point, viewSize: bounds.size)
        return session.raycast(origin: ray.origin, direction: ray.direction)?.position
    }

    /// Tap and drag tools: a touch that moves past the tap slop stops being a
    /// tap; for Slide Loop it starts the slide from the edge nearest to where the
    /// touch landed, then maps the drag onto the slide axis.
    private func trackToolTouch(_ touch: UITouch) {
        guard let renderer, let origin = touchOrigin else { return }
        let location = touch.location(in: self)
        if !touchTravelled, hypot(location.x - origin.location.x, location.y - origin.location.y) > Self.tapSlop {
            touchTravelled = true
            if touchTool == .slideLoop, let hit = origin.hit {
                if !edgeIsNear(hit, on: origin.location) {
                    session.flash("Start the drag closer to an edge")
                } else if let axis = session.beginSlideLoop(near: hit) {
                    // Like the grab brush: drag on a plane facing the camera through
                    // the start point, because the surface under the finger moves.
                    slide = (axis, (hit, -renderer.camera.forward))
                }
            }
        }
        guard let slide else { return }
        let ray = renderer.camera.ray(through: location, viewSize: bounds.size)
        let denom = simd_dot(ray.direction, slide.plane.normal)
        let axisSquared = simd_length_squared(slide.axis)
        guard abs(denom) > 1e-6, axisSquared > 0 else { return }
        let drag = ray.origin + ray.direction * (simd_dot(slide.plane.point - ray.origin, slide.plane.normal) / denom)
            - slide.plane.point
        session.updateSlideLoop(simd_dot(drag, slide.axis) / axisSquared)
    }

    /// Whether the cage edge the core picks for hit (the nearest in 3D, as in
    /// cruth_retopo_nearest_edge) lies within edgePickRadius of location on
    /// screen. With no cage it says yes, so the session can explain instead.
    private func edgeIsNear(_ hit: SIMD3<Float>, on location: CGPoint) -> Bool {
        guard let renderer else { return false }
        let edges = session.retopoEdges
        var nearest: (a: SIMD3<Float>, b: SIMD3<Float>)?
        var nearestDistance = Float.greatestFiniteMagnitude
        for i in stride(from: 0, to: edges.count - 1, by: 2) {
            let a = edges[i], ab = edges[i + 1] - a
            let t = simd_clamp(simd_dot(hit - a, ab) / max(simd_length_squared(ab), 1e-20), 0, 1)
            let d = simd_distance_squared(a + ab * t, hit)
            if d < nearestDistance {
                nearestDistance = d
                nearest = (a, edges[i + 1])
            }
        }
        guard let nearest else { return true }
        guard let a = renderer.camera.project(nearest.a, viewSize: bounds.size),
              let b = renderer.camera.project(nearest.b, viewSize: bounds.size) else { return false }
        let ab = CGPoint(x: b.x - a.x, y: b.y - a.y)
        let lengthSquared = ab.x * ab.x + ab.y * ab.y
        let t = lengthSquared > 0
            ? min(max(((location.x - a.x) * ab.x + (location.y - a.y) * ab.y) / lengthSquared, 0), 1) : 0
        return hypot(a.x + ab.x * t - location.x, a.y + ab.y * t - location.y) <= Self.edgePickRadius
    }

    private func addSamples(for touch: UITouch, event: UIEvent?) {
        guard let renderer else { return }
        let samples = event?.coalescedTouches(for: touch) ?? [touch]
        if session.mode != .sculpt {
            for sample in samples {
                let ray = renderer.camera.ray(through: sample.preciseLocation(in: self), viewSize: bounds.size)
                if let hit = session.raycast(origin: ray.origin, direction: ray.direction) { pathPoints.append(hit.position) }
            }
            refreshLivePath()
            return
        }
        for sample in samples {
            let point = sample.preciseLocation(in: self)
            let ray = renderer.camera.ray(through: point, viewSize: bounds.size)
            let pressure = Self.pressure(of: sample)

            if session.brush == .grab {
                // Grab drags along a plane facing the camera through the first hit,
                // because the surface under the pencil moves with the stroke.
                if let plane = grabPlane {
                    let denom = simd_dot(ray.direction, plane.normal)
                    guard abs(denom) > 1e-6 else { continue }
                    let t = simd_dot(plane.point - ray.origin, plane.normal) / denom
                    session.addSample(ray.origin + ray.direction * t, pressure: pressure)
                } else if let hit = session.raycast(origin: ray.origin, direction: ray.direction) {
                    grabPlane = (hit.position, -renderer.camera.forward)
                    session.addSample(hit.position, pressure: pressure)
                }
            } else if let hit = session.raycast(origin: ray.origin, direction: ray.direction) {
                session.addSample(hit.position, pressure: pressure)
            }
        }
        showCursor(at: touch.preciseLocation(in: self))
        setNeedsDisplay()
    }

    private static func pressure(of touch: UITouch) -> Float {
        guard touch.type == .pencil, touch.maximumPossibleForce > 0 else { return 1 }
        return Float(min(max(touch.force / touch.maximumPossibleForce * 1.5, 0.05), 1))
    }

    // MARK: Brush cursor

    private func showCursor(at point: CGPoint) {
        guard let renderer else { return }
        let ray = renderer.camera.ray(through: point, viewSize: bounds.size)
        guard let hit = session.raycast(origin: ray.origin, direction: ray.direction),
              let center = renderer.camera.project(hit.position, viewSize: bounds.size),
              let edge = renderer.camera.project(hit.position + renderer.camera.right * session.radius,
                                                 viewSize: bounds.size) else {
            cursor.isHidden = true
            return
        }
        let r = hypot(edge.x - center.x, edge.y - center.y)
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        cursor.path = UIBezierPath(ovalIn: CGRect(x: center.x - r, y: center.y - r, width: 2 * r, height: 2 * r)).cgPath
        cursor.strokeColor = (session.invert ? UIColor.systemTeal : UIColor.white).withAlphaComponent(0.85).cgColor
        cursor.isHidden = false
        CATransaction.commit()
    }
}
