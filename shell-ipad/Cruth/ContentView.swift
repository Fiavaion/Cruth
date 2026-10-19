import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @StateObject private var session = SculptSession()
    @State private var importing = false
    @State private var exportFile: MeshFile?
    @State private var showPrintCheck = false
    @State private var diagnosticsFiles: [URL] = []
    @State private var sharingDiagnostics = false
    @State private var confirmingNewSculpt = false
    @AppStorage("leftHanded") private var leftHanded = false
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        ZStack {
            ViewportView(session: session)
                .ignoresSafeArea()
                .accessibilityIdentifier("viewport")

            HStack(alignment: .center) {
                if leftHanded {
                    sidePanel
                    Spacer()
                    if session.mode == .sculpt { BrushPalette(session: session) }
                } else {
                    if session.mode == .sculpt { BrushPalette(session: session) }
                    Spacer()
                    sidePanel
                }
            }
            .padding(.horizontal, 12)

            VStack {
                topBar
                Spacer()
                statusBar
            }
            .padding(12)

            if session.isBusy {
                ProgressView(session.busyLabel)
                    .padding(20)
                    .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
                    .accessibilityIdentifier("busyIndicator")
            }
        }
        .fileImporter(isPresented: $importing,
                      allowedContentTypes: MeshFileFormat.allCases.map(\.contentType)) { result in
            if case let .success(url) = result { session.open(url: url) }
        }
        .fileExporter(isPresented: Binding(get: { exportFile != nil }, set: { if !$0 { exportFile = nil } }),
                      document: exportFile,
                      contentType: exportFile?.contentType ?? .data,
                      defaultFilename: exportFile?.filename ?? "Cruth") { result in
            if case let .failure(error) = result { session.message = error.localizedDescription }
        }
        .sheet(isPresented: $showPrintCheck) {
            PrintCheckView(session: session)
        }
        .sheet(isPresented: $sharingDiagnostics) {
            ShareSheet(items: diagnosticsFiles)
        }
        .alert("Start a new sculpt?", isPresented: $confirmingNewSculpt) {
            Button("New Sculpt", role: .destructive) { session.newSculpt() }
                .accessibilityIdentifier("newSculptConfirmButton")
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("The current sculpt and its autosave are discarded. Export it first to keep it.")
        }
        .onChange(of: scenePhase) { _, phase in session.scenePhaseChanged(to: phase) }
    }

    private var topBar: some View {
        HStack(spacing: 8) {
            Menu {
                Button("New Sculpt", systemImage: "circle.dashed") { confirmingNewSculpt = true }
                    .accessibilityIdentifier("newSculptButton")
                Button("Import…", systemImage: "square.and.arrow.down") { importing = true }
                Menu("Export", systemImage: "square.and.arrow.up") {
                    ForEach(MeshFileFormat.allCases) { format in
                        Button(format.rawValue.uppercased()) {
                            if let data = session.exportData(format) {
                                exportFile = MeshFile(data: data, contentType: format.contentType)
                            }
                        }
                    }
                }
                Menu("Export Game Asset", systemImage: "shippingbox") {
                    ForEach([1024, 2048, 4096], id: \.self) { size in
                        Button("\(size) px maps, 20k triangles") {
                            session.exportGameAsset(textureSize: Int32(size)) { folder in
                                exportFile = MeshFile(folder: folder)
                            }
                        }
                    }
                }
                Divider()
                Toggle("Left-Handed Layout", isOn: $leftHanded)
                Button("Share Diagnostics…", systemImage: "stethoscope") {
                    diagnosticsFiles = Diagnostics.shared.snapshot(summary: session.diagnosticsSummary())
                    sharingDiagnostics = true
                }
                .accessibilityIdentifier("shareDiagnosticsButton")
            } label: {
                Label("Cruth", systemImage: "cube.transparent")
                    .labelStyle(.titleAndIcon)
                    .font(.headline)
            }
            .disabled(session.isBusy)
            .accessibilityIdentifier("cruthMenu")

            Picker("Mode", selection: Binding(get: { session.mode }, set: { session.setMode($0) })) {
                ForEach(WorkMode.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            .frame(width: 240)
            .disabled(session.isBusy)
            .accessibilityIdentifier("modePicker")

            Spacer()

            // Everything here edits or reads the document, which a background
            // command owns while busy.
            Group {
                topologyMenu
                levelStepper
                Button { showPrintCheck = true } label: { Label("Print Check", systemImage: "printer") }
                    .accessibilityIdentifier("printCheckButton")
                Menu {
                    Button("Clear Mask") { session.clearMask() }
                    Button("Invert Mask") { session.invertMask() }
                } label: {
                    Label("Mask", systemImage: "theatermasks")
                }
            }
            .disabled(session.isBusy)

            Divider().frame(height: 24)

            Button { session.undo() } label: { Image(systemName: "arrow.uturn.backward") }
                .disabled(!session.undoAvailable)
                .keyboardShortcut("z", modifiers: .command)
                .disabled(session.isBusy)
                .accessibilityIdentifier("undoButton")
            Button { session.redo() } label: { Image(systemName: "arrow.uturn.forward") }
                .disabled(!session.redoAvailable)
                .keyboardShortcut("z", modifiers: [.command, .shift])
                .disabled(session.isBusy)
                .accessibilityIdentifier("redoButton")
        }
        .labelStyle(.iconOnly)
        .buttonStyle(.bordered)
        .padding(8)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14))
    }

    @ViewBuilder private var sidePanel: some View {
        switch session.mode {
        case .sculpt: BrushSettingsPanel(session: session)
        case .guides: GuidesPanel(session: session)
        case .retopo: RetopoPanel(session: session)
        }
    }

    /// Multires, voxel remesh and decimation (PRD S-01, S-02, S-04).
    private var topologyMenu: some View {
        Menu {
            Button("Subdivide (Add Level)", systemImage: "square.grid.3x3") { session.subdivide() }
            Toggle("Lock Base Level", isOn: $session.lockBaseLevel)
            Divider()
            Menu("Voxel Remesh", systemImage: "cube") {
                ForEach([64, 128, 192, 256, 384], id: \.self) { resolution in
                    Button("\(resolution) voxels") { session.voxelRemesh(resolution: Int32(resolution)) }
                }
            }
            Menu("Quad Remesh", systemImage: "squareshape.split.2x2") {
                ForEach([2000, 5000, 10000, 20000], id: \.self) { faces in
                    Button("\(faces.formatted()) quads") { session.quadRemesh(faces: UInt64(faces)) }
                }
                Divider()
                Text(session.guideStrokes.isEmpty ? "No guide strokes" : "\(session.guideStrokes.count) guide strokes")
            }
            Toggle("Keep Sculpt Detail", isOn: $session.keepDetail)
            Menu("Decimate", systemImage: "triangle") {
                Button("Keep 50%") { session.decimate(keeping: 0.5) }
                Button("Keep 25%") { session.decimate(keeping: 0.25) }
                Button("Keep 10%") { session.decimate(keeping: 0.1) }
            }
        } label: {
            Label("Topology", systemImage: "square.grid.3x3")
        }
        .accessibilityIdentifier("topologyMenu")
    }

    private var levelStepper: some View {
        HStack(spacing: 2) {
            Button { session.stepLevel(by: -1) } label: { Image(systemName: "minus") }
                .disabled(session.level == 0)
            Text("L\(session.level)/\(session.levelCount - 1)")
                .font(.caption.monospacedDigit())
                .frame(minWidth: 36)
            Button { session.stepLevel(by: 1) } label: { Image(systemName: "plus") }
                .disabled(session.level >= session.levelCount - 1)
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Multires level")
    }

    private var statusBar: some View {
        HStack {
            Text("\(session.vertexCount.formatted()) vertices · \(session.triangleCount.formatted()) triangles")
                .monospacedDigit()
                .accessibilityIdentifier("statusCounts")
            if let message = session.message {
                Text("·")
                Text(message).lineLimit(1)
                    .accessibilityIdentifier("statusMessage")
                Button { session.message = nil } label: { Image(systemName: "xmark.circle.fill") }
                    .buttonStyle(.plain)
            }
            Spacer()
            Text(session.fingerSculpting ? "Finger sculpts · drag background to orbit" : "Pencil sculpts · fingers navigate")
                .foregroundStyle(.secondary)
        }
        .font(.footnote)
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(.ultraThinMaterial, in: Capsule())
    }
}

/// Vertical tool strip (PRD I-06: floating, mirrorable).
struct BrushPalette: View {
    @ObservedObject var session: SculptSession

    var body: some View {
        VStack(spacing: 6) {
            ForEach(BrushKind.allCases) { kind in
                Button {
                    session.brush = kind
                } label: {
                    Image(systemName: kind.symbol)
                        .font(.title3)
                        .frame(width: 44, height: 44)
                        .background(session.brush == kind ? Color.accentColor.opacity(0.35) : .clear,
                                    in: RoundedRectangle(cornerRadius: 10))
                }
                .buttonStyle(.plain)
                .accessibilityLabel(kind.title)
                .help(kind.title)
            }
        }
        .padding(6)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14))
    }
}

struct BrushSettingsPanel: View {
    @ObservedObject var session: SculptSession

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(session.brush.title).font(.headline)
            labeledSlider("Radius", value: $session.radius, range: 0.01...0.6)
            labeledSlider("Strength", value: $session.strength, range: 0.0...1.0)
            Toggle("Invert", isOn: $session.invert)
            Toggle("Mirror X", isOn: $session.symmetryX)
            Toggle("Pressure → Size", isOn: $session.pressureSize)
            Toggle("Finger Sculpting", isOn: $session.fingerSculpting)
                .accessibilityIdentifier("fingerSculptingToggle")
        }
        .toggleStyle(.switch)
        .font(.subheadline)
        .frame(width: 220)
        .padding(12)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14))
    }

    private func labeledSlider(_ title: String, value: Binding<Float>, range: ClosedRange<Float>) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title)
                Spacer()
                Text(Double(value.wrappedValue), format: .number.precision(.fractionLength(2)))
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }
            Slider(value: value, in: range)
        }
    }
}

/// Export payload for `.fileExporter`: a single mesh file or a folder of
/// asset files (one exporter serves both, which SwiftUI handles best).
struct MeshFile: FileDocument {
    static var readableContentTypes: [UTType] { MeshFileFormat.allCases.map(\.contentType) + [.folder] }

    enum Payload {
        case data(Data)
        case folder(URL)
    }

    var payload: Payload
    var contentType: UTType
    var filename: String

    init(data: Data, contentType: UTType, filename: String = "Cruth") {
        payload = .data(data)
        self.contentType = contentType
        self.filename = filename
    }

    init(folder: URL) {
        payload = .folder(folder)
        contentType = .folder
        filename = folder.lastPathComponent
    }

    init(configuration: ReadConfiguration) throws {
        payload = .data(configuration.file.regularFileContents ?? Data())
        contentType = configuration.contentType
        filename = "Cruth"
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        switch payload {
        case .data(let data): return FileWrapper(regularFileWithContents: data)
        case .folder(let url): return try FileWrapper(url: url, options: .immediate)
        }
    }
}

/// Guide strokes for the quad remesher (PRD R-02).
struct GuidesPanel: View {
    @ObservedObject var session: SculptSession

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Guides").font(.headline)
            Text("Draw where edge loops should run. Quad Remesh follows them.")
                .font(.footnote)
                .foregroundStyle(.secondary)
            Text("\(session.guideStrokes.count) strokes").monospacedDigit()
                .accessibilityIdentifier("guideCount")
            Toggle("Draw with Finger", isOn: $session.fingerSculpting)
                .accessibilityIdentifier("fingerDrawingToggle")
            Button("Clear Guides", role: .destructive) { session.clearGuides() }
                .disabled(session.guideStrokes.isEmpty)
                .accessibilityIdentifier("clearGuidesButton")
        }
        .disabled(session.isBusy)
        .font(.subheadline)
        .frame(width: 220, alignment: .leading)
        .padding(12)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14))
    }
}

/// Manual retopology over the sculpt (PRD T-01..T-06).
struct RetopoPanel: View {
    @ObservedObject var session: SculptSession

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Retopology").font(.headline)
            toolPicker
            switch session.retopoTool {
            case .contour:
                Text("Stroke across a form to add an edge loop.").font(.footnote).foregroundStyle(.secondary)
                Stepper("Loop: \(session.loopVertices)", value: $session.loopVertices, in: 3...64)
            case .strip:
                Text("Stroke along a form to add a quad strip.").font(.footnote).foregroundStyle(.secondary)
                VStack(alignment: .leading, spacing: 2) {
                    Text("Width \(Double(session.stripWidth), format: .number.precision(.fractionLength(2)))")
                    Slider(value: $session.stripWidth, in: 0.01...0.5)
                }
                Stepper("Segments: \(session.stripSegments)", value: $session.stripSegments, in: 1...64)
            case .quadPen:
                Text("Tap four corners for a quad; every two more taps add the next one.")
                    .font(.footnote).foregroundStyle(.secondary)
                HStack {
                    let pending = session.quadPenPending.count
                    Text(pending == 1 ? "1 tap pending" : "\(pending) taps pending").monospacedDigit()
                        .accessibilityIdentifier("quadPenPending")
                    Spacer()
                    Button("Lift") { session.liftQuadPen() }
                        .disabled(session.quadPenPending.isEmpty)
                        .accessibilityIdentifier("retopoLiftButton")
                }
            case .slideLoop:
                Text("Drag from an edge to slide its loop.").font(.footnote).foregroundStyle(.secondary)
            case .insertLoop:
                Text("Tap an edge to cut a loop across it.").font(.footnote).foregroundStyle(.secondary)
            case .loopAround:
                Text("Draw round a feature over the cage to loop around it.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            Toggle("Draw with Finger", isOn: $session.fingerSculpting)
                .accessibilityIdentifier("fingerDrawingToggle")
            let s = session.retopoStats
            Text("\(s.quads) quads · \(s.triangles + s.ngons) other · \(s.poles) poles")
                .font(.footnote)
                .monospacedDigit()
                .accessibilityIdentifier("retopoStats")
            HStack {
                Button("Relax") { session.relaxRetopo() }
                Spacer()
                Button("Cancel", role: .cancel) { session.cancelRetopo() }
                    .accessibilityIdentifier("cancelRetopoButton")
                Button("Apply") { session.applyRetopo() }
                    .buttonStyle(.borderedProminent)
                    .accessibilityIdentifier("applyRetopoButton")
            }
            Toggle("Keep Sculpt Detail", isOn: $session.keepDetail)
        }
        // Cancel or a cage edit during Apply would free or change the cage it is reading.
        .disabled(session.isBusy)
        .toggleStyle(.switch)
        .font(.subheadline)
        .frame(width: 240, alignment: .leading)
        .padding(12)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14))
    }

    /// Six tools are too wide for one segmented control, so they sit in a
    /// two-column grid of buttons, each with a stable identifier.
    private var toolPicker: some View {
        LazyVGrid(columns: [GridItem(.flexible(), spacing: 6), GridItem(.flexible(), spacing: 6)], spacing: 6) {
            ForEach(RetopoTool.allCases) { tool in
                let selected = session.retopoTool == tool
                Button { session.retopoTool = tool } label: {
                    Text(tool.title)
                        .font(.footnote.weight(selected ? .semibold : .regular))
                        .lineLimit(1)
                        .minimumScaleFactor(0.6)  // shrink rather than truncate at large Dynamic Type
                        .frame(maxWidth: .infinity, minHeight: 36)
                        .background(selected ? Color.accentColor.opacity(0.35) : Color.secondary.opacity(0.12),
                                    in: RoundedRectangle(cornerRadius: 8))
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(selected ? .isSelected : [])
                .accessibilityIdentifier("retopoTool.\(tool.rawValue)")
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Tool")
        .accessibilityIdentifier("retopoToolPicker")
    }
}
