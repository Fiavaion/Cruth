import Combine
import Foundation
import simd
import UniformTypeIdentifiers

/// Brushes offered in the palette, mapped onto the core's C enum.
enum BrushKind: String, CaseIterable, Identifiable {
    case clay, standard, inflate, smooth, flatten, pinch, crease, grab, mask

    var id: String { rawValue }
    var title: String { rawValue.capitalized }

    var symbol: String {
        switch self {
        case .clay: return "hand.draw"
        case .standard: return "paintbrush.pointed"
        case .inflate: return "circle.circle"
        case .smooth: return "drop"
        case .flatten: return "square.3.layers.3d.down.right"
        case .pinch: return "arrow.down.right.and.arrow.up.left"
        case .crease: return "scribble"
        case .grab: return "hand.raised"
        case .mask: return "theatermasks"
        }
    }

    var core: CruthBrushType {
        switch self {
        case .clay: return CRUTH_BRUSH_CLAY
        case .standard: return CRUTH_BRUSH_STANDARD
        case .inflate: return CRUTH_BRUSH_INFLATE
        case .smooth: return CRUTH_BRUSH_SMOOTH
        case .flatten: return CRUTH_BRUSH_FLATTEN
        case .pinch: return CRUTH_BRUSH_PINCH
        case .crease: return CRUTH_BRUSH_CREASE
        case .grab: return CRUTH_BRUSH_GRAB
        case .mask: return CRUTH_BRUSH_MASK
        }
    }
}

enum MeshFileFormat: String, CaseIterable, Identifiable {
    case obj, stl, ply
    var id: String { rawValue }
    var contentType: UTType {
        switch self {
        case .obj: return .cruthOBJ
        case .stl: return .cruthSTL
        case .ply: return .cruthPLY
        }
    }
}

extension UTType {
    static let cruthOBJ = UTType("public.geometry-definition-format") ?? UTType(filenameExtension: "obj") ?? .data
    static let cruthSTL = UTType("public.standard-tesselated-geometry-format") ?? UTType(filenameExtension: "stl") ?? .data
    static let cruthPLY = UTType("public.polygon-file-format") ?? UTType(filenameExtension: "ply") ?? .data
}

/// Owns the core document and the brush state the UI edits. Every geometry
/// operation goes through the C API in `cruth.h`.
final class SculptSession: ObservableObject {
    private(set) var document: OpaquePointer?

    @Published var brush: BrushKind = .clay
    @Published var radius: Float = 0.12
    @Published var strength: Float = 0.5
    @Published var invert = false
    @Published var symmetryX = true
    @Published var pressureSize = false
    @Published var fingerSculpting = false

    @Published private(set) var canUndo = false
    @Published private(set) var canRedo = false
    @Published private(set) var vertexCount = 0
    @Published private(set) var triangleCount = 0
    @Published private(set) var level = 0
    @Published private(set) var levelCount = 1
    @Published var lockBaseLevel = false {
        didSet { if let document { cruth_document_set_base_locked(document, lockBaseLevel) } }
    }
    @Published var message: String?
    /// True while a long command runs off the main thread; input is ignored meanwhile.
    @Published private(set) var isBusy = false
    @Published private(set) var busyLabel = ""

    /// Bumped when a new mesh replaces the old one, so the viewport can re-frame.
    @Published private(set) var documentGeneration = 0

    private var brushBeforeSqueeze: BrushKind?

    init() {
        newSphere()
    }

    deinit {
        if let document { cruth_document_destroy(document) }
    }

    // MARK: Documents

    func newSphere() {
        guard !isBusy else { return }
        var doc: OpaquePointer?
        let status = cruth_document_create_sphere(5, 1.0, &doc)
        replace(with: doc, status: status)
    }

    func open(url: URL) {
        guard !isBusy else { return }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        var doc: OpaquePointer?
        let status = cruth_document_load(url.path(percentEncoded: false), &doc)
        replace(with: doc, status: status)
        if status == CRUTH_OK { message = "Opened \(url.lastPathComponent)" }
    }

    /// Serialises the current mesh through the core's exporter.
    func exportData(_ format: MeshFileFormat) -> Data? {
        guard let document else { return nil }
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("cruth-export-\(UUID().uuidString).\(format.rawValue)")
        defer { try? FileManager.default.removeItem(at: url) }
        let status = cruth_document_save(document, url.path(percentEncoded: false))
        guard status == CRUTH_OK else {
            report(status)
            return nil
        }
        return try? Data(contentsOf: url)
    }

    /// Decimates the most detailed level, unwraps it and bakes normal, AO and
    /// height maps from the sculpt into a new folder, off the main thread.
    func exportGameAsset(textureSize: Int32, completion: @escaping (URL) -> Void) {
        guard let document, !isBusy else { return }
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("Cruth Asset \(textureSize)px")
        try? FileManager.default.removeItem(at: folder)
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        } catch {
            message = error.localizedDescription
            return
        }
        isBusy = true
        busyLabel = "Baking \(textureSize) px maps…"
        let path = folder.path(percentEncoded: false)
        DispatchQueue.global(qos: .userInitiated).async {
            var report = CruthAssetReport()
            let status = cruth_document_export_game_asset(document, path, "cruth", 20_000, textureSize, &report)
            let detail = String(cString: cruth_last_error())
            DispatchQueue.main.async {
                self.isBusy = false
                if status == CRUTH_OK {
                    self.message = "Baked \(report.triangles) triangles, \(report.charts) UV charts"
                    completion(folder)
                } else {
                    let summary = String(cString: cruth_status_string(status))
                    self.message = detail.isEmpty ? summary : "\(summary): \(detail)"
                }
            }
        }
    }

    private func replace(with doc: OpaquePointer?, status: CruthStatus) {
        guard status == CRUTH_OK, let doc else {
            report(status)
            return
        }
        if let document { cruth_document_destroy(document) }
        document = doc
        cruth_document_set_base_locked(doc, lockBaseLevel)
        documentGeneration += 1
        refresh()
    }

    // MARK: Strokes

    var coreBrush: CruthBrush {
        var b = cruth_brush_default(brush.core)
        b.radius = radius
        b.strength = strength
        b.invert = invert
        b.pressure_affects_radius = pressureSize
        b.symmetry = symmetryX ? UInt32(CRUTH_SYMMETRY_X) : 0
        return b
    }

    func beginStroke() {
        guard let document, !isBusy else { return }
        var b = coreBrush
        _ = cruth_stroke_begin(document, &b)
    }

    func addSample(_ position: SIMD3<Float>, pressure: Float) {
        guard let document else { return }
        var sample = CruthStrokeSample(position: CruthVec3(x: position.x, y: position.y, z: position.z),
                                       pressure: pressure)
        _ = cruth_stroke_add_sample(document, &sample)
    }

    func endStroke() {
        guard let document else { return }
        _ = cruth_stroke_end(document)
        refresh()
    }

    func raycast(origin: SIMD3<Float>, direction: SIMD3<Float>) -> (position: SIMD3<Float>, normal: SIMD3<Float>)? {
        guard let document, !isBusy else { return nil }
        var hit = CruthRayHit()
        let found = cruth_document_raycast(document,
                                           CruthVec3(x: origin.x, y: origin.y, z: origin.z),
                                           CruthVec3(x: direction.x, y: direction.y, z: direction.z),
                                           &hit)
        guard found else { return nil }
        return (SIMD3(hit.position.x, hit.position.y, hit.position.z),
                SIMD3(hit.normal.x, hit.normal.y, hit.normal.z))
    }

    /// Pencil Pro squeeze: hold-free toggle into Smooth and back.
    func toggleSmooth() {
        if let previous = brushBeforeSqueeze {
            brush = previous
            brushBeforeSqueeze = nil
        } else {
            brushBeforeSqueeze = brush
            brush = .smooth
        }
    }

    // MARK: Commands

    func undo() {
        guard let document, !isBusy, cruth_undo(document) else { return }
        refresh()
    }

    func redo() {
        guard let document, !isBusy, cruth_redo(document) else { return }
        refresh()
    }

    /// Adds a multires level on top of the stack and moves to it (PRD S-01).
    func subdivide() {
        guard let document, !isBusy else { return }
        if cruth_document_vertex_count(document) > 2_000_000 {
            message = "Mesh is already dense; subdividing again would exceed the phase-0 budget."
            return
        }
        runInBackground("Subdividing…") { cruth_document_subdivide($0, 1) }
    }

    func stepLevel(by delta: Int) {
        guard let document, !isBusy else { return }
        let target = Int32(level + delta)
        guard target >= 0, target < Int32(levelCount) else { return }
        run(cruth_document_set_level(document, target))
    }

    /// Rebuilds the mesh as an even, closed quad surface (PRD S-02). Drops multires levels.
    func voxelRemesh(resolution: Int32) {
        runInBackground("Voxel remeshing at \(resolution)…", done: "Remeshed at \(resolution) voxels") {
            cruth_document_voxel_remesh($0, resolution)
        }
    }

    /// Field-aligned all-quad remesh of the most detailed level (PRD R-01). Drops multires levels.
    func quadRemesh(faces: UInt64) {
        runInBackground("Quad remeshing to \(faces) faces…", done: "Quad remeshed") {
            cruth_document_quad_remesh($0, faces, false)
        }
    }

    /// Reduces the triangle count by a fraction (PRD S-04). Drops multires levels.
    func decimate(keeping fraction: Double) {
        guard let document, !isBusy else { return }
        let target = max(4, UInt64(Double(cruth_document_triangle_count(document)) * fraction))
        runInBackground("Decimating…") { cruth_document_decimate($0, target) }
    }

    /// Runs a long document command off the main thread. While it runs, input,
    /// other commands, picking and drawing are suspended, so nothing else
    /// touches the document.
    private func runInBackground(_ label: String, done: String? = nil,
                                 _ work: @escaping (OpaquePointer) -> CruthStatus) {
        guard let document, !isBusy else { return }
        isBusy = true
        busyLabel = label
        DispatchQueue.global(qos: .userInitiated).async {
            let status = work(document)
            let detail = String(cString: cruth_last_error())
            DispatchQueue.main.async {
                self.isBusy = false
                if status == CRUTH_OK {
                    self.message = done
                    self.refresh()
                } else {
                    let summary = String(cString: cruth_status_string(status))
                    self.message = detail.isEmpty ? summary : "\(summary): \(detail)"
                }
            }
        }
    }

    private func run(_ status: CruthStatus) {
        if status == CRUTH_OK {
            message = nil
            refresh()
        } else {
            report(status)
        }
    }

    func clearMask() {
        guard let document, !isBusy else { return }
        cruth_mask_clear(document)
        refresh()
    }

    func invertMask() {
        guard let document, !isBusy else { return }
        cruth_mask_invert(document)
        refresh()
    }

    func printReport() -> CruthPrintReport? {
        guard let document else { return nil }
        var result = CruthPrintReport()
        let status = cruth_document_check_print(document, &result)
        guard status == CRUTH_OK else {
            report(status)
            return nil
        }
        return result
    }

    func repairForPrint() -> CruthRepairLog? {
        guard let document, !isBusy else { return nil }
        var log = CruthRepairLog()
        let status = cruth_document_repair_print(document, &log)
        guard status == CRUTH_OK else {
            report(status)
            return nil
        }
        refresh()
        return log
    }

    // MARK: Helpers

    func bounds() -> (center: SIMD3<Float>, radius: Float) {
        var lo = CruthVec3(), hi = CruthVec3()
        cruth_document_bounds(document, &lo, &hi)
        let a = SIMD3(lo.x, lo.y, lo.z), b = SIMD3(hi.x, hi.y, hi.z)
        return ((a + b) / 2, simd_length(b - a) / 2)
    }

    private func refresh() {
        guard let document else { return }
        canUndo = cruth_can_undo(document)
        canRedo = cruth_can_redo(document)
        vertexCount = cruth_document_vertex_count(document)
        triangleCount = cruth_document_triangle_count(document)
        level = Int(cruth_document_level(document))
        levelCount = Int(cruth_document_level_count(document))
    }

    private func report(_ status: CruthStatus) {
        let detail = String(cString: cruth_last_error())
        let summary = String(cString: cruth_status_string(status))
        message = detail.isEmpty ? summary.capitalized : "\(summary.capitalized): \(detail)"
    }
}
