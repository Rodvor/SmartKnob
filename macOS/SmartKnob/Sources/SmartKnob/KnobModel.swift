import AppKit
import CoreBluetooth
import Combine

struct FoundKnob: Identifiable { let peripheral: CBPeripheral; var id: UUID { peripheral.identifier }; var name: String { peripheral.name ?? "SmartKnob" } }
final class KnobModel: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let service = CBUUID(string: "7b4e1000-8e5a-4d2b-a6b9-4d534b4e4f42")
    static let commandID = CBUUID(string: "7b4e1001-8e5a-4d2b-a6b9-4d534b4e4f42")
    static let eventsID = CBUUID(string: "7b4e1002-8e5a-4d2b-a6b9-4d534b4e4f42")
    static let configID = CBUUID(string: "7b4e1003-8e5a-4d2b-a6b9-4d534b4e4f42")
    @Published var draft = Configuration()
    @Published var deviceConfig: Configuration?
    @Published var devices: [FoundKnob] = []
    @Published var connection = "Starting Bluetooth…"
    @Published var ready = false
    @Published var applying = false
    @Published var message = "Connect your knob to read its saved settings."
    @Published var currentPage = "—"
    @Published var hidReady = false
    @Published var awake = true
    @Published var volume: Double?
    @Published var brightness: Double?
    @Published var logs: [String] = []
    @Published var mediaTarget: MediaTarget = MediaTarget(rawValue: UserDefaults.standard.string(forKey: "mediaTarget") ?? "") ?? .hid {
        didSet { UserDefaults.standard.set(mediaTarget.rawValue, forKey: "mediaTarget") }
    }
    @Published var displayID: UInt32 = UInt32(UserDefaults.standard.integer(forKey: "brightnessDisplay")) {
        didSet { brightnessControl.selected = displayID; UserDefaults.standard.set(Int(displayID), forKey: "brightnessDisplay") }
    }
    var displays: [(UInt32,String)] { brightnessControl.displays }
    var dirty: Bool { deviceConfig != draft }
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var command: CBCharacteristic?, events: CBCharacteristic?, configuration: CBCharacteristic?
    private var timer: Timer?
    private var writes: [Data] = []
    private var writing = false
    private var writeAt = Date.distantPast
    private var applyAt = Date.distantPast
    private var pendingConfig: Configuration?
    private var transaction: UInt8 = 0
    private var ack = [0,0]
    private var lastEvent = 0
    private var lastStatus = Date.distantPast
    private var userDisconnected = false
    private var unsupportedVolume = false, unsupportedBrightness = false
    private let audio = AudioControl(), brightnessControl = BrightnessControl(), media = MediaControl()
    override init() {
        super.init()
        if ProcessInfo.processInfo.arguments.contains("--preview") { connection = "Interface preview · Bluetooth off"; return }
        central = CBCentralManager(delegate: self, queue: .main)
        let t = Timer(timeInterval: 0.5, repeats: true) { [weak self] _ in self?.tick() }
        RunLoop.main.add(t, forMode: .common); timer = t
    }
    func log(_ text: String) {
        message = text
        let f = DateFormatter(); f.dateFormat = "HH:mm:ss"
        logs.insert("\(f.string(from: Date()))  \(text)", at: 0)
        if logs.count > 60 { logs.removeLast() }
    }
    func scan() {
        guard central?.state == .poweredOn else { return }
        userDisconnected = false; devices = []
        connection = "Looking for SmartKnob…"
        let connected = central.retrieveConnectedPeripherals(withServices: [Self.service, CBUUID(string: "1812")])
        for p in connected where p.name == "Haptic Knob" || p.identifier.uuidString == UserDefaults.standard.string(forKey: "knobID") { found(p) }
        central.scanForPeripherals(withServices: [Self.service, CBUUID(string: "1812")], options: nil)
    }
    private func found(_ p: CBPeripheral) {
        if !devices.contains(where: { $0.id == p.identifier }) { devices.append(FoundKnob(peripheral: p)) }
        if peripheral == nil && !userDisconnected && p.identifier.uuidString == UserDefaults.standard.string(forKey: "knobID") { connect(p) }
    }
    func connect(_ p: CBPeripheral) {
        if let old = peripheral, old != p { central.cancelPeripheralConnection(old) }
        reset(); userDisconnected = false; peripheral = p; p.delegate = self
        connection = "Connecting to \(p.name ?? "SmartKnob")…"
        central.connect(p, options: nil)
    }
    func disconnect() {
        userDisconnected = true; central.stopScan()
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        reset(); peripheral = nil; connection = "Disconnected"
    }
    private func reset() {
        ready = false; command = nil; events = nil; configuration = nil
        writes = []; writing = false; applying = false; pendingConfig = nil
        ack = [0,0]; lastEvent = 0; hidReady = false; currentPage = "—"
        unsupportedVolume = false; unsupportedBrightness = false
    }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn { scan() }
        else { reset(); peripheral = nil; connection = central.state == .unauthorized ? "Allow Bluetooth in System Settings → Privacy & Security" : "Bluetooth is unavailable or off" }
    }
    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        if name == "Haptic Knob" || p.name == "Haptic Knob" { found(p) }
    }
    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        guard p == peripheral else { return }
        central.stopScan(); connection = "Reading services…"; p.discoverServices([Self.service])
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        guard p == peripheral else { return }; reset(); peripheral = nil
        connection = "Connection failed"; log(error?.localizedDescription ?? "Could not connect. Scan to retry.")
    }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        guard p == peripheral else { return }
        reset(); peripheral = nil; connection = "Disconnected"; log(error?.localizedDescription ?? "Knob disconnected")
        if !userDisconnected { scan() }
    }
    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard p == peripheral else { return }
        guard error == nil, let service = p.services?.first(where: { $0.uuid == Self.service }) else {
            connection = "Companion firmware required"; log("This knob does not expose the companion service. Install the new firmware first."); return
        }
        p.discoverCharacteristics([Self.commandID, Self.eventsID, Self.configID], for: service)
    }
    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard p == peripheral else { return }
        for c in service.characteristics ?? [] {
            if c.uuid == Self.commandID { command = c }
            if c.uuid == Self.eventsID { events = c }
            if c.uuid == Self.configID { configuration = c }
        }
        guard error == nil, command != nil, let events, configuration != nil else { log("Required Bluetooth characteristics are missing."); return }
        p.setNotifyValue(true, for: events)
    }
    func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard p == peripheral else { return }
        guard error == nil, characteristic.isNotifying, let configuration else { log("Could not subscribe to knob events."); return }
        p.readValue(for: configuration)
    }
    func peripheral(_ p: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard p == peripheral else { return }
        guard error == nil, let data = characteristic.value else { log(error?.localizedDescription ?? "Empty Bluetooth response"); return }
        let b = [UInt8](data)
        if characteristic.uuid == Self.configID {
            guard let config = Configuration(bytes: b) else { log("Unsupported or invalid configuration version."); return }
            let firstRead = !ready
            let preserveDraft = deviceConfig != nil && dirty
            deviceConfig = config
            if !preserveDraft { draft = config.supportedSteps }
            ready = true; connection = "Connected"; lastStatus = Date()
            UserDefaults.standard.set(p.identifier.uuidString, forKey: "knobID")
            if firstRead { enqueue([4]) }
            log("Read saved configuration from knob."); tick(); return
        }
        guard let kind = b.first else { return }
        if kind == 0x81 && b.count == 9 && b[1] == 1 {
            currentPage = KnobPage(rawValue: Int(b[2]))?.name ?? "Unknown"; hidReady = b[3] != 0; awake = b[4] != 0; lastStatus = Date()
        } else if kind == 0x82 && b.count == 3 && applying && b[1] == transaction {
            applying = false
            if b[2] == 0, let sent = pendingConfig {
                deviceConfig = sent; log("Applied and saved on knob. No restart needed.")
                if let configuration { p.readValue(for: configuration) }
            } else { log("Knob rejected the configuration or could not save it. Previous settings remain active.") }
            pendingConfig = nil
        } else if kind == 0x80 && b.count == 7 && ready {
            let seq = read16(b,1); guard seq != lastEvent else { return }; lastEvent = seq
            let page = Int(b[3]), action = Int(b[4])
            if action == 0 && (page == 0 || page == 2) {
                let value = Double(read16(b,5))/10000
                guard value <= 1 else { return }
                let ok = page == 0 ? audio.set(value) : brightnessControl.set(value)
                ack[page == 0 ? 0 : 1] = seq
                if !ok {
                    if page == 0 { unsupportedVolume = true } else { unsupportedBrightness = true }
                    log("\(page == 0 ? "Volume" : "Brightness") write failed; control disabled until reconnect.")
                }
            } else if (1...3).contains(action) {
                if mediaTarget == .hid { enqueue([3,UInt8(action-1)]) }
                else { media.perform(action-1, target: mediaTarget) { [weak self] error in if let error { self?.log(error) } } }
            }
        }
    }
    func apply() {
        guard ready, !applying, draft.valid else { return }
        transaction &+= 1; applying = true; applyAt = Date(); pendingConfig = draft
        let b = draft.bytes
        enqueue([10,transaction])
        for start in stride(from: 0, to: b.count, by: 17) {
            enqueue([11,transaction,UInt8(start)] + Array(b[start..<min(start+17,b.count)]))
        }
        enqueue([12,transaction] + le32(draft.hash)); log("Applying configuration…")
    }
    func revert() { if let deviceConfig { draft = deviceConfig.supportedSteps } }
    private func enqueue(_ b: [UInt8]) {
        guard ready, b.count <= 20 else { return }
        writes.append(Data(b)); pump()
    }
    private func pump() {
        guard !writing, !writes.isEmpty, let peripheral, let command else { return }
        writing = true; writeAt = Date(); peripheral.writeValue(writes.removeFirst(), for: command, type: .withResponse)
    }
    func peripheral(_ p: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard p == peripheral, characteristic.uuid == Self.commandID else { return }
        writing = false
        if let error {
            writes = []; applying = false; pendingConfig = nil
            log("Bluetooth write failed: \(error.localizedDescription)"); return
        }
        pump()
    }
    private func tick() {
        volume = unsupportedVolume ? nil : audio.read()
        brightness = unsupportedBrightness ? nil : brightnessControl.read()
        guard ready else { return }
        if writing && Date().timeIntervalSince(writeAt) > 5 {
            log("Bluetooth write timed out. Reconnecting…")
            if let peripheral { central.cancelPeripheralConnection(peripheral) }; return
        }
        if applying {
            if Date().timeIntervalSince(applyAt) > 7 {
                applying = false; pendingConfig = nil
                log("Apply acknowledgement timed out. Reading back the knob's configuration.")
                if let peripheral, let configuration { peripheral.readValue(for: configuration) }
            }
            return
        }
        guard writes.isEmpty, !writing else { return }
        if Date().timeIntervalSince(lastStatus) > 8 {
            log("Knob heartbeat timed out. Reconnecting…")
            if let peripheral { central.cancelPeripheralConnection(peripheral) }; return
        }
        let caps = (volume != nil ? 1 : 0) | (brightness != nil ? 2 : 0) | (mediaTarget != .hid ? 4 : 0)
        enqueue([1, UInt8(caps)])
        if let volume { enqueue([2,0] + le16(Int((volume*10000).rounded())) + le16(ack[0])) }
        if let brightness { enqueue([2,2] + le16(Int((brightness*10000).rounded())) + le16(ack[1])) }
    }
}
