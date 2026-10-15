import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @StateObject private var session = SculptSession()
    @State private var importing = false
    @State private var exportFile: MeshFile?
    @State private var showPrintCheck = false
    @AppStorage("leftHanded") private var leftHanded = false

    var body: some View {
        ZStack {
            ViewportView(session: session)
                .ignoresSafeArea()

            HStack(alignment: .center) {
                if leftHanded {
                    BrushSettingsPanel(session: session)
                    Spacer()
                    BrushPalette(session: session)
                } else {
                    BrushPalette(session: session)
                    Spacer()
                    BrushSettingsPanel(session: session)
                }
            }
            .padding(.horizontal, 12)

            VStack {
                topBar
                Spacer()
                statusBar
            }
            .padding(12)
        }
        .fileImporter(isPresented: $importing,
                      allowedContentTypes: MeshFileFormat.allCases.map(\.contentType)) { result in
            if case let .success(url) = result { session.open(url: url) }
        }
        .fileExporter(isPresented: Binding(get: { exportFile != nil }, set: { if !$0 { exportFile = nil } }),
                      document: exportFile,
                      contentType: exportFile?.contentType ?? .data,
                      defaultFilename: "Cruth") { result in
            if case let .failure(error) = result { session.message = error.localizedDescription }
        }
        .sheet(isPresented: $showPrintCheck) {
            PrintCheckView(session: session)
        }
    }

    private var topBar: some View {
        HStack(spacing: 8) {
            Menu {
                Button("New Sphere", systemImage: "circle.dashed") { session.newSphere() }
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
                Divider()
                Toggle("Left-Handed Layout", isOn: $leftHanded)
            } label: {
                Label("Cruth", systemImage: "cube.transparent")
                    .labelStyle(.titleAndIcon)
                    .font(.headline)
            }

            Spacer()

            topologyMenu
            levelStepper
            Button { showPrintCheck = true } label: { Label("Print Check", systemImage: "printer") }
            Menu {
                Button("Clear Mask") { session.clearMask() }
                Button("Invert Mask") { session.invertMask() }
            } label: {
                Label("Mask", systemImage: "theatermasks")
            }

            Divider().frame(height: 24)

            Button { session.undo() } label: { Image(systemName: "arrow.uturn.backward") }
                .disabled(!session.canUndo)
                .keyboardShortcut("z", modifiers: .command)
            Button { session.redo() } label: { Image(systemName: "arrow.uturn.forward") }
                .disabled(!session.canRedo)
                .keyboardShortcut("z", modifiers: [.command, .shift])
        }
        .labelStyle(.iconOnly)
        .buttonStyle(.bordered)
        .padding(8)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14))
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
            Menu("Decimate", systemImage: "triangle") {
                Button("Keep 50%") { session.decimate(keeping: 0.5) }
                Button("Keep 25%") { session.decimate(keeping: 0.25) }
                Button("Keep 10%") { session.decimate(keeping: 0.1) }
            }
        } label: {
            Label("Topology", systemImage: "square.grid.3x3")
        }
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
            if let message = session.message {
                Text("·")
                Text(message).lineLimit(1)
                Button { session.message = nil } label: { Image(systemName: "xmark.circle.fill") }
                    .buttonStyle(.plain)
            }
            Spacer()
            Text(session.fingerSculpting ? "Finger sculpts" : "Pencil sculpts · fingers navigate")
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

/// Export payload for `.fileExporter`.
struct MeshFile: FileDocument {
    static var readableContentTypes: [UTType] { MeshFileFormat.allCases.map(\.contentType) }

    var data: Data
    var contentType: UTType

    init(data: Data, contentType: UTType) {
        self.data = data
        self.contentType = contentType
    }

    init(configuration: ReadConfiguration) throws {
        data = configuration.file.regularFileContents ?? Data()
        contentType = configuration.contentType
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }
}
