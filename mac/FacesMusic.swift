import AppKit
import ApplicationServices
import Carbon

func emit(_ value: [String: Any]) {
    let data = try! JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
    print(String(data: data, encoding: .utf8)!)
}
func attribute(_ e: AXUIElement, _ name: String) -> CFTypeRef? {
    var result: CFTypeRef?
    guard AXUIElementCopyAttributeValue(e, name as CFString, &result) == .success else { return nil }
    return result
}
func children(_ e: AXUIElement) -> [AXUIElement] {
    attribute(e, kAXChildrenAttribute) as? [AXUIElement] ?? []
}
func title(_ e: AXUIElement) -> String { attribute(e, kAXTitleAttribute) as? String ?? "" }
let actions = ["toggle", "previous", "next", "favorite", "volume_up", "volume_down"]
func netease(_ pid: pid_t, _ command: String, _ action: String) -> [String: Any] {
    guard AXIsProcessTrusted() else {
        return ["ok": false, "actions": [], "error": "需要为 FacesMusicHelper 开启辅助功能权限", "code": "accessibility_required"]
    }
    let app = AXUIElementCreateApplication(pid)
    AXUIElementSetMessagingTimeout(app, 0.7)
    guard let raw = attribute(app, kAXMenuBarAttribute) else { return ["ok": false, "error": "读不到网易云菜单"] }
    let bar = raw as! AXUIElement
    // Inspect only Control -> menu items. No activation, window raising, mouse,
    // coordinates, or keyboard injection; the user's editor retains focus.
    guard let control = children(bar).first(where: { ["控制", "Control", "Controls"].contains(title($0)) }) else {
        return ["ok": false, "error": "网易云控制菜单不可用"]
    }
    let items = children(control).flatMap { children($0) }
    let names: [String: [String]] = [
        "toggle": ["播放", "暂停", "Play", "Pause"],
        "previous": ["上一个", "Previous"], "next": ["下一个", "Next"],
        "favorite": ["喜欢歌曲", "取消喜欢", "取消喜欢歌曲", "Like", "Unlike"],
        "volume_up": ["升高音量", "Increase Volume"], "volume_down": ["降低音量", "Decrease Volume"]]
    var targets: [String: AXUIElement] = [:]
    for (key, labels) in names {
        targets[key] = items.first { labels.contains(title($0)) && (attribute($0, kAXEnabledAttribute) as? Bool ?? false) }
    }
    // NetEase 3.x exposes explicit repeat commands, but no selected marker.
    // Never infer current mode from a successful AX dispatch.
    if let repeatMenu = items.first(where: { title($0) == "循环播放" }) {
        let options = children(repeatMenu).flatMap { children($0) }
        for (key, label) in ["repeat_one": "单曲", "repeat_all": "全部"] {
            targets[key] = options.first { title($0) == label && (attribute($0, kAXEnabledAttribute) as? Bool ?? false) }
        }
    }
    // Only an explicit unlike label is evidence of a selected heart. A generic
    // "Like song" menu title alone is not proof that the song is unliked.
    var favorite: Any = NSNull()
    if let item = targets["favorite"], ["取消喜欢", "取消喜欢歌曲", "Unlike"].contains(title(item)) { favorite = true }
    if command == "status" { return ["ok": true, "actions": Array(targets.keys).sorted(), "favorite": favorite] }
    guard let target = targets[action] else { return ["ok": false, "error": "播放器暂不支持这个操作", "code": "unavailable"] }
    let result = AXUIElementPerformAction(target, kAXPressAction as CFString)
    return result == .success ? ["ok": true, "pending": true] : ["ok": false, "error": "网易云未接受操作", "code": "action_failed"]
}
func favoriteResult(_ result: NSAppleEventDescriptor?) -> [String: Any]? {
    guard let result = result, (result.numberOfItems == 4 || result.numberOfItems == 5), let liked = result.atIndex(1),
          [typeBoolean, typeTrue, typeFalse].contains(liked.descriptorType) else { return nil }
    return ["favorite": liked.booleanValue,
            "favorite_track": ["title": result.atIndex(2)?.stringValue ?? "",
                               "artist": result.atIndex(3)?.stringValue ?? "",
                               "album": result.atIndex(4)?.stringValue ?? ""]]
}
func scriptString(_ text: String) -> String {
    return "\"" + text.replacingOccurrences(of: "\\", with: "\\\\")
        .replacingOccurrences(of: "\"", with: "\\\"")
        .replacingOccurrences(of: "\n", with: "\\n")
        .replacingOccurrences(of: "\r", with: "\\r") + "\""
}
func apple(_ command: String, _ action: String, _ request: [String: String]) -> [String: Any] {
    if command == "status" {
        var status: [String: Any] = ["ok": true, "actions": actions + ["repeat_one", "repeat_all"], "repeat_mode": NSNull(), "favorite": NSNull(), "favorite_readback": "permission_required"]
        // Background reads must not repeatedly ask for Automation permission.
        let target = NSAppleEventDescriptor(bundleIdentifier: "com.apple.Music")
        guard let desc = target.aeDesc,
              AEDeterminePermissionToAutomateTarget(desc, typeWildCard, typeWildCard, false) == noErr else {
            return status
        }
        status["favorite_readback"] = "unavailable"
        var error: NSDictionary?
        let script = NSAppleScript(source: """
        with timeout of 2 seconds
            tell application id "com.apple.Music"
                set repeatMode to "off"
                if song repeat is one then set repeatMode to "one"
                if song repeat is all then set repeatMode to "all"
                set favoriteInfo to {}
                try
                    set t to current track
                    set favoriteInfo to {favorited of t, name of t, artist of t, album of t}
                end try
                return {repeatMode, favoriteInfo}
            end tell
        end timeout
        """)
        let result = script?.executeAndReturnError(&error)
        if error == nil, let result = result, result.numberOfItems == 2 {
            if let mode = result.atIndex(1)?.stringValue, ["off", "one", "all"].contains(mode) { status["repeat_mode"] = mode }
            if let readback = favoriteResult(result.atIndex(2)) {
                status.merge(readback) { _, new in new }
                status["favorite_readback"] = "ready"
            }
        }
        if let error = error { status["favorite_read_error"] = error[NSAppleScript.errorNumber] }
        return status
    }
    if ["repeat_one", "repeat_all"].contains(action) {
        let desired = action == "repeat_one" ? "one" : "all"
        let source = """
        with timeout of 2 seconds
            tell application id "com.apple.Music"
                set song repeat to \(desired)
                if song repeat is one then return "one"
                if song repeat is all then return "all"
                return "off"
            end tell
        end timeout
        """
        var error: NSDictionary?
        let result = NSAppleScript(source: source)?.executeAndReturnError(&error)
        guard error == nil else { return ["ok": false, "code": "uncertain", "error": "循环设置结果未知，请检查 Apple Music"] }
        let mode = result?.stringValue ?? ""
        guard ["off", "one", "all"].contains(mode) else { return ["ok": true, "pending": true] }
        return ["ok": true, "pending": mode != desired, "repeat_mode": mode]
    }
    if action == "favorite", let expectedTitle = request["expected_title"],
       let expectedArtist = request["expected_artist"], let expectedAlbum = request["expected_album"] {
        // All strings are literal data; pin the track object before the mutation.
        let source = """
        with timeout of 2 seconds
            tell application id "com.apple.Music"
                set t to current track
                if (name of t is not \(scriptString(expectedTitle))) or (artist of t is not \(scriptString(expectedArtist))) or (album of t is not \(scriptString(expectedAlbum))) then return "track_changed"
                set desiredFavorite to not (favorited of t)
                set favorited of t to desiredFavorite
                try
                    return {favorited of t, name of t, artist of t, album of t, desiredFavorite}
                on error
                    return {}
                end try
            end tell
        end timeout
        """
        var error: NSDictionary?
        guard let script = NSAppleScript(source: source) else { return ["ok": false, "code": "invalid_script"] }
        let result = script.executeAndReturnError(&error)
        if error != nil { return ["ok": false, "code": "uncertain", "error": "收藏结果未知，请检查 Apple Music"] }
        if result.stringValue == "track_changed" { return ["ok": false, "code": "track_changed", "error": "歌曲已变化，请稍后再按"] }
        var receipt: [String: Any] = ["ok": true, "pending": true]
        if let readback = favoriteResult(result), let desired = result.atIndex(5),
           [typeBoolean, typeTrue, typeFalse].contains(desired.descriptorType),
           (readback["favorite"] as? Bool) == desired.booleanValue {
            receipt.merge(readback) { _, new in new }
            receipt["pending"] = false
        }
        return receipt
    }
    let commands = ["toggle": "playpause", "previous": "previous track", "next": "next track",
                    "favorite": "set favorited of current track to not (favorited of current track)",
                    "volume_up": "set sound volume to (get sound volume) + 5",
                    "volume_down": "set sound volume to (get sound volume) - 5"]
    guard let script = commands[action] else { return ["ok": false, "error": "unknown action"] }
    var error: NSDictionary?
    NSAppleScript(source: "tell application id \"com.apple.Music\"\n\(script)\nend tell")?.executeAndReturnError(&error)
    if error != nil { return ["ok": false, "error": "Apple Music 未接受操作，请检查自动化权限", "code": "automation_required"] }
    return ["ok": true, "pending": true]
}
if CommandLine.arguments.contains("--doctor") {
    emit(["ok": true, "accessibility": AXIsProcessTrusted()]); exit(0)
}
do {
    let data = FileHandle.standardInput.readDataToEndOfFile()
    guard data.count < 4096, let request = try JSONSerialization.jsonObject(with: data) as? [String: String],
          let source = request["source"], ["com.netease.163music", "com.apple.Music"].contains(source),
          let command = request["command"], ["status", "action"].contains(command) else {
        emit(["ok": false, "error": "invalid request"]); exit(1)
    }
    guard let app = NSRunningApplication.runningApplications(withBundleIdentifier: source).first else {
        emit(["ok": false, "error": "播放器未启动", "actions": []]); exit(0)
    }
    if let expectedPID = request["expected_pid"], expectedPID != String(app.processIdentifier) {
        emit(["ok": false, "code": "track_changed", "error": "播放器已变化，请稍后再按"]); exit(0)
    }
    let action = request["action"] ?? ""
    emit(source == "com.netease.163music" ? netease(app.processIdentifier, command, action) : apple(command, action, request))
} catch { emit(["ok": false, "error": "invalid request"]); exit(1) }
