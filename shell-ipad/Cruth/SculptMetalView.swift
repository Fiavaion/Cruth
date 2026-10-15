import MetalKit
import simd
import UIKit

/// The viewport. Input follows the PRD rule "Pencil makes geometry, fingers
/// move the camera" (PRD I-01, I-04):
///
/// - Apple Pencil: raw coalesced touches with pressure become stroke samples.
/// - One finger orbits, two fingers pan and pinch-zoom.
/// - Two-finger tap undoes, three-finger tap redoes.
/// - Pencil double-tap toggles invert; Pencil Pro squeeze toggles Smooth.
/// - Hover (M2+ iPads) shows the brush footprint before contact.
///
/// With "finger sculpting" on, a single finger sculpts instead of orbiting.
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
    private let guideLayer = CAShapeLayer()
    private let retopoLayer = CAShapeLayer()
    private let pathLayer = CAShapeLayer()
    private var pathPoints: [SIMD3<Float>] = []

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

        for (overlay, color, width) in [(guideLayer, UIColor.systemOrange, 3.0),
                                        (retopoLayer, UIColor.systemTeal, 1.5),
                                        (pathLayer, UIColor.systemYellow, 2.0)] {
            overlay.fillColor = UIColor.clear.cgColor
            overlay.strokeColor = color.withAlphaComponent(0.9).cgColor
            overlay.lineWidth = CGFloat(width)
            overlay.lineJoin = .round
            overlay.lineCap = .round
            layer.addSublayer(overlay)
        }

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
        orbitGesture.isEnabled = !session.fingerSculpting
        refreshOverlay()
        setNeedsDisplay()
    }

    /// Projects guides and the retopology cage into the view. Occlusion is not
    /// tested yet; strokes behind the model show through.
    func refreshOverlay() {
        guard let renderer else { return }
        let size = bounds.size
        func polyline(_ points: [SIMD3<Float>], into path: UIBezierPath) {
            var started = false
            for p in points {
                guard let q = renderer.camera.project(p, viewSize: size) else {
                    started = false
                    continue
                }
                if started { path.addLine(to: q) } else { path.move(to: q) }
                started = true
            }
        }
        let guides = UIBezierPath()
        for stroke in session.guideStrokes { polyline(stroke, into: guides) }
        let cage = UIBezierPath()
        let edges = session.retopoEdges
        for i in stride(from: 0, to: edges.count - 1, by: 2) { polyline([edges[i], edges[i + 1]], into: cage) }
        let live = UIBezierPath()
        polyline(pathPoints, into: live)
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        guideLayer.path = guides.cgPath
        retopoLayer.path = cage.cgPath
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
        return touch.type == .pencil || (session.fingerSculpting && touch.type == .direct)
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesBegan(touches, with: event)
        if strokeTouch != nil, session.fingerSculpting, (event?.allTouches?.count ?? 0) > 1,
           strokeTouch?.type == .direct {
            // A second finger means navigation, not sculpting.
            finishStroke()
            return
        }
        guard strokeTouch == nil, let touch = touches.first(where: sculpts) else { return }
        strokeTouch = touch
        grabPlane = nil
        pathPoints.removeAll()
        if session.mode == .sculpt { session.beginStroke() }
        addSamples(for: touch, event: event)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesMoved(touches, with: event)
        guard let touch = strokeTouch, touches.contains(touch) else { return }
        addSamples(for: touch, event: event)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesEnded(touches, with: event)
        guard let touch = strokeTouch, touches.contains(touch) else { return }
        addSamples(for: touch, event: event)
        finishStroke()
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesCancelled(touches, with: event)
        guard let touch = strokeTouch, touches.contains(touch) else { return }
        finishStroke()
    }

    private func finishStroke() {
        if session.mode == .sculpt {
            session.endStroke()
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

    private func addSamples(for touch: UITouch, event: UIEvent?) {
        guard let renderer else { return }
        let samples = event?.coalescedTouches(for: touch) ?? [touch]
        if session.mode != .sculpt {
            for sample in samples {
                let ray = renderer.camera.ray(through: sample.preciseLocation(in: self), viewSize: bounds.size)
                if let hit = session.raycast(origin: ray.origin, direction: ray.direction) { pathPoints.append(hit.position) }
            }
            refreshOverlay()
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
