import SwiftUI

/// Hosts the Metal viewport inside SwiftUI.
struct ViewportView: UIViewRepresentable {
    @ObservedObject var session: SculptSession

    func makeUIView(context: Context) -> SculptMetalView {
        SculptMetalView(session: session)
    }

    func updateUIView(_ view: SculptMetalView, context: Context) {
        view.sessionDidChange()
    }
}
