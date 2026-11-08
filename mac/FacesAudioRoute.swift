import AppKit
import ApplicationServices
import CoreAudio

struct arFailure: Error { let code: String; let message: String }
func arFail(_ code: String, _ message: String) throws -> Never {
    throw arFailure(code: code, message: message)
}
func arEmit(_ object: [String: Any]) {
    let data = try! JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
    print(String(data: data, encoding: .utf8)!)
}
func arAddress(_ selector: AudioObjectPropertySelector) -> AudioObjectPropertyAddress {
    AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal,
                               mElement: kAudioObjectPropertyElementMain)
}
func arNumber(_ id: AudioObjectID, _ selector: AudioObjectPropertySelector) -> UInt32 {
    var a = arAddress(selector), n: UInt32 = 4, result: UInt32 = 0
    _ = AudioObjectGetPropertyData(id, &a, 0, nil, &n, &result)
    return result
}
func arText(_ id: AudioObjectID, _ selector: AudioObjectPropertySelector) -> String {
    var a = arAddress(selector), n = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
    var result: Unmanaged<CFString>?
    guard AudioObjectGetPropertyData(id, &a, 0, nil, &n, &result) == noErr,
          let result = result else { return "" }
    return result.takeUnretainedValue() as String
}
let arSystem = AudioObjectID(kAudioObjectSystemObject)
func arCurrent() -> AudioObjectID { arNumber(arSystem, kAudioHardwarePropertyDefaultOutputDevice) }
func arSnapshot(_ id: AudioObjectID) -> [String: Any] {
    ["name": arText(id, kAudioObjectPropertyName), "uid": arText(id, kAudioDevicePropertyDeviceUID),
     "airplay": arNumber(id, kAudioDevicePropertyTransportType) == kAudioDeviceTransportTypeAirPlay]
}
func arDevices() -> [AudioObjectID] {
    var a = arAddress(kAudioHardwarePropertyDevices), n: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(arSystem, &a, 0, nil, &n) == noErr else { return [] }
    var ids = [AudioObjectID](repeating: 0, count: Int(n) / 4)
    guard AudioObjectGetPropertyData(arSystem, &a, 0, nil, &n, &ids) == noErr else { return [] }
    return ids
}
func arGet(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var result: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, name as CFString, &result) == .success else { return nil }
    return result
}
func arFindTable(_ element: AXUIElement, _ depth: Int = 0) -> AXUIElement? {
    guard depth < 18 else { return nil }
    if arGet(element, "AXIdentifier") as? String == "OutputSourceTable" { return element }
    let role = arGet(element, "AXRole") as? String ?? ""
    if role == "AXMenuBar" || role == "AXMenu" { return nil }
    for child in arGet(element, "AXChildren") as? [AXUIElement] ?? [] {
        if let found = arFindTable(child, depth + 1) { return found }
    }
    return nil
}
// AirPlay rows may be absent from AXChildren/AXRows but discoverable by hit testing.
// Only accept exact labels owned by System Settings inside its current output table.
func arMatchingRows(_ table: AXUIElement, _ pid: pid_t, _ name: String) -> [AXUIElement] {
    guard let rawPosition = arGet(table, "AXPosition"), let rawSize = arGet(table, "AXSize") else { return [] }
    var position = CGPoint.zero, size = CGSize.zero
    guard AXValueGetValue(rawPosition as! AXValue, .cgPoint, &position),
          AXValueGetValue(rawSize as! AXValue, .cgSize, &size), size.width > 40, size.height > 0 else { return [] }
    var matches: [AXUIElement] = []
    let root = AXUIElementCreateSystemWide()
    for y in stride(from: position.y, to: position.y + size.height, by: 4) {
        var hit: AXUIElement?
        guard AXUIElementCopyElementAtPosition(root, Float(position.x + 24), Float(y), &hit) == .success,
              var cursor = hit, arGet(cursor, "AXValue") as? String == name else { continue }
        var owner: pid_t = 0
        guard AXUIElementGetPid(cursor, &owner) == .success, owner == pid else { continue }
        for _ in 0..<6 {
            if arGet(cursor, "AXRole") as? String == "AXRow" {
                if !matches.contains(where: { CFEqual($0, cursor) }) { matches.append(cursor) }
                break
            }
            guard let parent = arGet(cursor, "AXParent") else { break }
            cursor = parent as! AXUIElement
        }
    }
    return matches
}
func arWaitForOutput(_ predicate: (AudioObjectID) -> Bool) -> AudioObjectID? {
    let deadline = Date().addingTimeInterval(15)
    repeat {
        let id = arCurrent()
        if id != 0 && predicate(id) { return id }
        RunLoop.current.run(until: Date().addingTimeInterval(0.2))
    } while Date() < deadline
    return nil
}
func audioRouteMain(_ args: [String]) {
do {
    guard let command = args.first, ["status", "select", "restore"].contains(command),
          args.count == (command == "status" ? 1 : 2) else {
        try arFail("usage", "status | select <exact speaker name> | restore <device UID>")
    }
    let before = arCurrent()
    guard before != 0 else { try arFail("audio_unavailable", "Cannot read the system audio output") }
    if command == "status" { arEmit(["ok": true, "output": arSnapshot(before)]); exit(0) }
    if command == "restore" {
        guard let target = arDevices().first(where: { arText($0, kAudioDevicePropertyDeviceUID) == args[1] }) else {
            try arFail("restore_unavailable", "Original audio output is unavailable; output left unchanged")
        }
        var a = arAddress(kAudioHardwarePropertyDefaultOutputDevice), id = target
        guard AudioObjectSetPropertyData(arSystem, &a, 0, nil, 4, &id) == noErr,
              arWaitForOutput({ $0 == target }) != nil else {
            try arFail("uncertain", "Output restoration was not confirmed; do not retry as a toggle")
        }
        arEmit(["ok": true, "previous": arSnapshot(before), "output": arSnapshot(target)]); exit(0)
    }
    guard AXIsProcessTrusted() else { try arFail("accessibility_required", "Accessibility permission is required") }
    // Selecting an already-active AirPlay output might disconnect it. Refuse ambiguity.
    guard arNumber(before, kAudioDevicePropertyTransportType) != kAudioDeviceTransportTypeAirPlay else {
        try arFail("already_airplay", "AirPlay is already active; restore the saved output first")
    }
    let previous = arSnapshot(before)
    let previousApp = NSWorkspace.shared.frontmostApplication
    let settingsBundle = "com.apple.systempreferences"
    let discoveryStarted = Date()
    var windowRestored = false
    var restoreMilliseconds = 0
    func restoreWorkspace() {
        guard !windowRestored else { return }
        windowRestored = true
        restoreMilliseconds = Int(Date().timeIntervalSince(discoveryStarted) * 1000)
        // If Settings was already the user's foreground app, leave it available.
        guard previousApp?.bundleIdentifier != settingsBundle else { return }
        let foreground = NSWorkspace.shared.frontmostApplication
        let settings = NSRunningApplication.runningApplications(withBundleIdentifier: settingsBundle).first
        // Do not steal focus if the user switched to a third app during discovery.
        let shouldRestoreFocus = foreground?.bundleIdentifier == settingsBundle
        _ = settings?.hide()
        if shouldRestoreFocus, let previousApp = previousApp, !previousApp.isTerminated {
            _ = previousApp.activate(options: [])
        }
    }
    defer { restoreWorkspace() }
    guard NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.Sound-Settings.extension?output")!) else {
        try arFail("settings_unavailable", "Cannot open Sound settings")
    }
    let deadline = Date().addingTimeInterval(10)
    var selected: AXUIElement?
    repeat {
        if let app = NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.systempreferences").first {
            let root = AXUIElementCreateApplication(app.processIdentifier)
            AXUIElementSetMessagingTimeout(root, 0.5)
            if let table = arFindTable(root) {
                let rows = arMatchingRows(table, app.processIdentifier, args[1])
                if rows.count > 1 { try arFail("ambiguous", "Multiple matching output rows; output left unchanged") }
                if let row = rows.first { selected = row; break }
            }
        }
        RunLoop.current.run(until: Date().addingTimeInterval(0.25))
    } while Date() < deadline
    guard let row = selected else { try arFail("not_found", "Target is not visible in Sound settings; output left unchanged") }
    guard arCurrent() == before else { try arFail("output_changed", "Output changed during discovery; no selection sent") }
    var settable = DarwinBoolean(false)
    guard AXUIElementIsAttributeSettable(row, "AXSelected" as CFString, &settable) == .success, settable.boolValue,
          AXUIElementSetAttributeValue(row, "AXSelected" as CFString, kCFBooleanTrue) == .success else {
        try arFail("selection_failed", "System did not accept output selection")
    }
    // The selection is dispatched. Hide immediately, before the AirPlay handshake.
    restoreWorkspace()
    guard let output = arWaitForOutput({ arNumber($0, kAudioDevicePropertyTransportType) == kAudioDeviceTransportTypeAirPlay }),
          arGet(row, "AXSelected") as? Bool == true else {
        arEmit(["ok": false, "code": "uncertain", "previous": previous,
              "error": "Connection not confirmed; do not automatically repeat selection"]); exit(1)
    }
    arEmit(["ok": true, "target": args[1], "previous": previous, "output": arSnapshot(output),
            "workspace_restore_ms": restoreMilliseconds,
            "settings_hidden": NSRunningApplication.runningApplications(withBundleIdentifier: settingsBundle).first?.isHidden ?? true,
            "foreground_restored": NSWorkspace.shared.frontmostApplication?.processIdentifier == previousApp?.processIdentifier])
} catch let error as arFailure {
    arEmit(["ok": false, "code": error.code, "error": error.message]); exit(1)
} catch { arEmit(["ok": false, "code": "unexpected", "error": String(describing: error)]); exit(1) }

}
