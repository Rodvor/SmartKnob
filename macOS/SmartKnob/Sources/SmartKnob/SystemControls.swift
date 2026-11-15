import AppKit
import CoreAudio
import IOKit
import IOKit.graphics

// Always target the current default output. Fixed-volume HDMI/USB devices are
// unavailable, not reported as a fictional software volume.
final class AudioControl {
    private func device() -> AudioDeviceID? {
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultOutputDevice, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
        var id: AudioDeviceID = 0; var size = UInt32(MemoryLayout.size(ofValue: id))
        return AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &id) == noErr && id != 0 ? id : nil
    }
    private func controls(_ id: AudioDeviceID) -> [AudioObjectPropertyAddress] {
        let candidates: [UInt32] = [0, 1, 2]
        let supported = candidates.compactMap { channel -> AudioObjectPropertyAddress? in
            var a = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyVolumeScalar, mScope: kAudioDevicePropertyScopeOutput, mElement: channel)
            var writable: DarwinBoolean = false
            guard AudioObjectHasProperty(id, &a), AudioObjectIsPropertySettable(id, &a, &writable) == noErr, writable.boolValue else { return nil }
            return a
        }
        if let master = supported.first, master.mElement == 0 { return [master] }
        return supported
    }
    func read() -> Double? {
        guard let id = device() else { return nil }
        let values = controls(id).compactMap { address -> Double? in
            var a = address; var value: Float32 = 0; var size = UInt32(MemoryLayout<Float32>.size)
            guard AudioObjectGetPropertyData(id, &a, 0, nil, &size, &value) == noErr else { return nil }
            return Double(value)
        }
        return values.isEmpty ? nil : values.max()
    }
    func set(_ level: Double) -> Bool {
        guard let id = device() else { return false }
        let addresses = controls(id)
        guard !addresses.isEmpty else { return false }
        // Preserve channel balance when a device exposes individual channels.
        let old = addresses.map { address -> Float32 in
            var a = address; var v: Float32 = 0; var n = UInt32(MemoryLayout<Float32>.size)
            _ = AudioObjectGetPropertyData(id, &a, 0, nil, &n, &v); return v
        }
        let peak = old.max() ?? 0
        var ok = true
        for (index, address) in addresses.enumerated() {
            var a = address
            var value = Float32(min(1,max(0,level))) * (peak > 0 ? old[index]/peak : 1)
            if AudioObjectSetPropertyData(id, &a, 0, nil, UInt32(MemoryLayout<Float32>.size), &value) != noErr { ok = false }
        }
        return ok
    }
}

final class BrightnessControl {
    // DisplayServices is dynamically resolved: Apple does not offer a uniform
    // public brightness API on modern Apple Silicon displays. Fail closed.
    private typealias Get = @convention(c) (UInt32, UnsafeMutablePointer<Float>) -> Int32
    private typealias Set = @convention(c) (UInt32, Float) -> Int32
    private let library = dlopen("/System/Library/PrivateFrameworks/DisplayServices.framework/DisplayServices", RTLD_LAZY)
    var selected: UInt32 = UInt32(UserDefaults.standard.integer(forKey: "brightnessDisplay"))
    var displays: [(UInt32, String)] {
        NSScreen.screens.compactMap { screen in
            guard let n = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber else { return nil }
            return (n.uint32Value, screen.localizedName)
        }
    }
    private var display: UInt32 { selected == 0 ? CGMainDisplayID() : selected }
    func read() -> Double? {
        guard displays.contains(where: { $0.0 == display }), let library,
              let get = dlsym(library, "DisplayServicesGetBrightness"),
              dlsym(library, "DisplayServicesSetBrightness") != nil else { return nil }
        var value: Float = 0
        guard unsafeBitCast(get, to: Get.self)(display, &value) == 0, value.isFinite, value >= 0, value <= 1 else { return nil }
        return Double(value)
    }
    func set(_ value: Double) -> Bool {
        guard let library, let set = dlsym(library, "DisplayServicesSetBrightness"), read() != nil else { return false }
        return unsafeBitCast(set, to: Set.self)(display, Float(min(1,max(0,value)))) == 0
    }
}

enum MediaTarget: String, CaseIterable { case hid = "System media keys", music = "Apple Music", spotify = "Spotify" }
final class MediaControl {
    private let worker = DispatchQueue(label: "SmartKnob.media")
    private var pending = 0
    func perform(_ action: Int, target: MediaTarget, completion: @escaping (String?) -> Void) {
        let bundle = target == .music ? "com.apple.Music" : "com.spotify.client"
        // Avoid launching a player unexpectedly on a dial turn.
        guard !NSRunningApplication.runningApplications(withBundleIdentifier: bundle).isEmpty else {
            completion("Open \(target.rawValue), or select System media keys."); return
        }
        let command = ["playpause", "next track", "previous track"][min(2,max(0,action))]
        guard pending < 4 else { completion("Media queue full; skipped a stale turn."); return }
        pending += 1
        let queuedAt = Date()
        worker.async {
            guard Date().timeIntervalSince(queuedAt) < 0.75 else {
                DispatchQueue.main.async { self.pending -= 1; completion("Skipped a delayed media action.") }; return
            }
            var error: NSDictionary?
            NSAppleScript(source: "tell application id \"\(bundle)\" to \(command)")?.executeAndReturnError(&error)
            let message = error.map { "Media control: \($0[NSAppleScript.errorMessage] ?? $0)" }
            DispatchQueue.main.async { self.pending -= 1; completion(message) }
        }
    }
}
