import Carbon
import Cocoa
import InputMethodKit

// Entry point of Pyaw, the Myanglish input method. macOS launches this bundle from
// ~/Library/Input Methods when the input source is used.
//
//   Pyaw --register   tell macOS about this bundle so it shows up in System Settings right away

if CommandLine.arguments.contains("--register") {
    let status = TISRegisterInputSource(Bundle.main.bundleURL as CFURL)
    print(status == noErr ? "Registered \(Bundle.main.bundlePath)" : "TISRegisterInputSource failed: \(status)")
    exit(status == noErr ? 0 : 1)
}

let connectionName = Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String ?? "Pyaw_Connection"
let bundleID = Bundle.main.bundleIdentifier ?? "io.github.dolphinsareweird.inputmethod.Pyaw"

guard let server = IMKServer(name: connectionName, bundleIdentifier: bundleID) else {
    NSLog("Pyaw: could not start IMKServer '\(connectionName)'")
    exit(1)
}
EngineHost.shared.loadInBackground()

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
withExtendedLifetime(server) {
    app.run()
}
