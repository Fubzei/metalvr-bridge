// MetalVR Bridge Launcher
// macOS App — your buddy downloads this .app, runs it before launching Steam games
// Has a triangle test toggle, live error log, system info, and Steam launcher

import SwiftUI
import AppKit

@main
struct MetalVRBridgeApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 720, minHeight: 560)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 800, height: 620)
    }
}

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
}
