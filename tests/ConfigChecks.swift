import Foundation
@main struct ConfigurationChecks {
    static func main() {
        let c = Configuration()
        precondition(c.bytes == [1,10,0,4,0,1,2,3,2,50,60,30,14,1,5,40,50,20,14,1,4,40,40,20,14,1,5,60,150,3,14,1,0,104,4,104,6,104])
        precondition(c.hash == 0x0e7c33f7)
        precondition(Configuration(bytes: c.bytes) == c)
        for count in 0..<c.bytes.count { precondition(Configuration(bytes: Array(c.bytes.prefix(count))) == nil) }
        let mutations: [(Int,UInt8)] = [(0,2),(3,0),(3,5),(4,9),(5,0),(8,0),(8,3),(9,81),(10,0),(11,2),(12,255),(13,0),(29,4),(32,16),(33,0)]
        for (offset,value) in mutations {
            var bad = c.bytes; bad[offset] = value
            precondition(Configuration(bytes: bad) == nil, "Offset \(offset) = \(value)")
        }
        var legacy = c
        legacy.pages[0].step = 1; legacy.pages[2].step = 5
        precondition(legacy.supportedSteps.pages[0].step == 2 && legacy.supportedSteps.pages[2].step == 4)
        precondition(legacy.supportedSteps.shortcuts == legacy.shortcuts)
        var ordered = c
        ordered.setEnabled(1,false); precondition(ordered.order == [0,2,3,1] && ordered.count == 3)
        ordered.setEnabled(1,true); precondition(ordered.count == 4)
        ordered.setEnabled(0,false); ordered.setEnabled(2,false); ordered.setEnabled(3,false); ordered.setEnabled(1,false)
        precondition(ordered.count == 1 && ordered.valid)
        precondition(Configuration(bytes: ordered.bytes) == ordered)
        print("PASS: Swift/C++ wire fixture, validation, page ordering and last-page preservation")
    }
}
