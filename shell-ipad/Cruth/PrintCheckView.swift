import SwiftUI

/// 3D-print check and one-tap repair (PRD X-01, X-02).
struct PrintCheckView: View {
    @ObservedObject var session: SculptSession
    @Environment(\.dismiss) private var dismiss
    @State private var report: CruthPrintReport?
    @State private var repairSummary: String?

    var body: some View {
        NavigationStack {
            List {
                if let r = report {
                    Section {
                        Label(r.printable ? "Ready to print" : (r.watertight ? "Needs repair" : "Not watertight"),
                              systemImage: r.printable ? "checkmark.seal.fill" : "exclamationmark.triangle.fill")
                            .foregroundStyle(r.printable ? Color.green : Color.orange)
                            .font(.headline)
                    }
                    Section("Mesh") {
                        row("Vertices", r.vertices)
                        row("Triangles", r.triangles)
                        row("Shells", r.shells)
                        LabeledContent("Size", value: size(r))
                        LabeledContent("Volume", value: r.volume.formatted(.number.precision(.significantDigits(4))))
                    }
                    Section("Problems") {
                        row("Holes", r.holes)
                        row("Non-manifold edges", r.non_manifold_edges)
                        row("Non-manifold vertices", r.non_manifold_vertices)
                        row("Inconsistent winding", r.inconsistent_edges)
                        row("Degenerate faces", r.degenerate_faces)
                        row("Unused vertices", r.unreferenced_vertices)
                    }
                    Section {
                        Text("Self-intersections, wall thickness and overhangs are not checked yet.")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                }
                if let repairSummary {
                    Section("Last repair") { Text(repairSummary).font(.footnote) }
                }
            }
            .navigationTitle("Print Check")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
                ToolbarItem(placement: .primaryAction) {
                    Button("Repair") { repair() }
                        .disabled(report?.printable ?? true)
                }
            }
            .onAppear { report = session.printReport() }
        }
    }

    private func repair() {
        guard let log = session.repairForPrint() else { return }
        repairSummary = """
            Welded \(log.welded_vertices) vertices, removed \(log.removed_degenerate_faces) degenerate and \
            \(log.removed_duplicate_faces) duplicate faces, flipped \(log.flipped_faces) faces, filled \
            \(log.filled_holes) holes, turned \(log.inverted_shells) shells right side out.
            """
        report = session.printReport()
    }

    private func row(_ title: String, _ value: UInt64) -> some View {
        LabeledContent(title) {
            Text(value.formatted())
                .monospacedDigit()
                .foregroundStyle(value == 0 || !isProblem(title) ? Color.secondary : Color.orange)
        }
    }

    private func isProblem(_ title: String) -> Bool {
        !["Vertices", "Triangles", "Shells"].contains(title)
    }

    private func size(_ r: CruthPrintReport) -> String {
        let extent = [r.bounds_max.x - r.bounds_min.x, r.bounds_max.y - r.bounds_min.y, r.bounds_max.z - r.bounds_min.z]
        return extent.map { Double($0).formatted(.number.precision(.significantDigits(3))) }.joined(separator: " × ")
    }
}
