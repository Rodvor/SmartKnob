import Foundation

enum KnobPage: Int, CaseIterable, Identifiable {
    case volume, media, brightness, discord
    var id: Int { rawValue }
    var name: String { ["Volume", "Media", "Brightness", "Discord"][rawValue] }
    var icon: String { ["speaker.wave.2.fill", "playpause.fill", "sun.max.fill", "headphones"][rawValue] }
    var bounded: Bool { self == .volume || self == .brightness }
}
struct PageSettings: Equatable {
    var step: Int = 5
    var strength: Int = 40
    var buildup: Int = 50
    var notches: Int = 20
    var arc: Int = 270
}
struct Shortcut: Equatable {
    var modifiers: Int = 0
    var key: Int = 0x68
}
struct Configuration: Equatable {
    static let size = 38
    var sleepMinutes = 10
    var count = 4
    var order = [0, 1, 2, 3]
    var pages = [PageSettings(step: 2, strength: 50, buildup: 60, notches: 30),
                 PageSettings(), PageSettings(step: 4, buildup: 40),
                 PageSettings(strength: 60, buildup: 150, notches: 3)]
    var shortcuts = [Shortcut(), Shortcut(modifiers: 4), Shortcut(modifiers: 6)]
    var valid: Bool {
        guard (0...120).contains(sleepMinutes), (1...4).contains(count), order.count == 4,
              Set(order) == Set(0...3), pages.count == 4, shortcuts.count == 3 else { return false }
        return pages.allSatisfy { p in
            [1,2,4,5,10,20,25].contains(p.step) && (0...80).contains(p.strength) &&
            (1...200).contains(p.buildup) && (3...100).contains(p.notches) && (90...300).contains(p.arc)
        } && pages[3].notches == 3 && shortcuts.allSatisfy { (0...15).contains($0.modifiers) && (4...0x73).contains($0.key) }
    }
    var bytes: [UInt8] {
        precondition(valid)
        var b: [UInt8] = [1] + le16(sleepMinutes) + [UInt8(count)] + order.map(UInt8.init)
        for p in pages { b += [UInt8(p.step), UInt8(p.strength), UInt8(p.buildup), UInt8(p.notches)] + le16(p.arc) }
        for s in shortcuts { b += [UInt8(s.modifiers), UInt8(s.key)] }
        return b
    }
    init() {}
    init?(bytes b: [UInt8]) {
        guard b.count == Self.size, b[0] == 1 else { return nil }
        sleepMinutes = read16(b, 1); count = Int(b[3]); order = b[4..<8].map(Int.init)
        pages = (0..<4).map { i in
            let j = 8+i*6
            return PageSettings(step: Int(b[j]), strength: Int(b[j+1]), buildup: Int(b[j+2]), notches: Int(b[j+3]), arc: read16(b,j+4))
        }
        shortcuts = (0..<3).map { Shortcut(modifiers: Int(b[32+$0*2]), key: Int(b[33+$0*2])) }
        guard valid else { return nil }
    }
    var supportedSteps: Configuration {
        var c = self
        for i in [0,2] { c.pages[i].step = c.pages[i].step <= 2 ? 2 : 4 }
        return c
    }
    var hash: UInt32 { bytes.reduce(2_166_136_261) { ($0 ^ UInt32($1)) &* 16_777_619 } }
    mutating func setEnabled(_ id: Int, _ enabled: Bool) {
        guard let i = order.firstIndex(of: id) else { return }
        if enabled && i >= count { order.remove(at: i); order.insert(id, at: count); count += 1 }
        if !enabled && i < count && count > 1 { order.remove(at: i); count -= 1; order.insert(id, at: count) }
    }
}
func le16(_ value: Int) -> [UInt8] { [UInt8(truncatingIfNeeded: value), UInt8(truncatingIfNeeded: value >> 8)] }
func read16(_ bytes: [UInt8], _ offset: Int) -> Int { Int(bytes[offset]) | Int(bytes[offset+1]) << 8 }
func le32(_ v: UInt32) -> [UInt8] { (0..<4).map { UInt8(truncatingIfNeeded: v >> ($0*8)) } }
