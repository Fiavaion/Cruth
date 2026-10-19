import SwiftUI

@main
struct CruthApp: App {
    init() {
        Log.session.info("Launch Cruth \(Diagnostics.appVersion), core \(String(cString: cruth_version())), " +
                         "\(Diagnostics.systemVersion), \(Diagnostics.deviceModel)")
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .preferredColorScheme(.dark)
        }
    }
}
