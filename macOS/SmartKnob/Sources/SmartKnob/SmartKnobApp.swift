import SwiftUI
import ServiceManagement

@main
struct SmartKnobApp: App {
    @StateObject private var model = KnobModel()
    var body: some Scene {
        Window("SmartKnob", id: "settings") {
            ConfigurationView().environmentObject(model)
                .frame(minWidth: 800, minHeight: 640)
        }.defaultSize(width: 920, height: 720)
        MenuBarExtra("SmartKnob", systemImage: "dial.medium.fill") {
            MenuContent().environmentObject(model)
        }
    }
}
struct MenuContent: View {
    @EnvironmentObject var model: KnobModel
    @Environment(\.openWindow) var openWindow
    var body: some View {
        Text(model.connection)
        if model.ready {
            Text("Page: \(model.currentPage) · \(model.awake ? "Awake" : "Sleeping")")
            Text("HID: \(model.hidReady ? "Ready" : "Not ready")")
            Text(model.dirty ? "Configuration: unapplied changes" : "Configuration: synced")
        }
        Divider()
        Button("Open SmartKnob…") { openWindow(id: "settings"); NSApp.activate(ignoringOtherApps: true) }
        Button(model.ready ? "Disconnect" : "Scan for knob") { if model.ready { model.disconnect() } else { model.scan() } }
        Divider()
        Button("Quit SmartKnob") { NSApp.terminate(nil) }.keyboardShortcut("q")
    }
}
final class WindowState: ObservableObject {
    @Published var selected = 0
    @Published var login = SMAppService.mainApp.status == .enabled
}
struct ConfigurationView: View {
    @EnvironmentObject var model: KnobModel
    @StateObject private var ui = WindowState()
    private var selected: Int { get { ui.selected } nonmutating set { ui.selected = newValue } }
    private var login: Bool { get { ui.login } nonmutating set { ui.login = newValue } }
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 14) {
                Image(systemName: "dial.medium.fill").font(.system(size: 35)).foregroundStyle(.tint)
                VStack(alignment: .leading, spacing: 3) {
                    Text("SmartKnob").font(.title2.bold())
                    Text("Make every turn your own.").foregroundStyle(.secondary)
                }
                Spacer()
                Circle().fill(model.ready ? .green : .orange).frame(width: 8,height: 8)
                Text(model.connection).font(.callout).foregroundStyle(.secondary)
            }.padding(24)
            Divider()
            HSplitView {
                VStack(alignment: .leading, spacing: 14) {
                    Button { selected = 4 } label: {
                        Label("General", systemImage: "gearshape")
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(10)
                            .contentShape(Rectangle())
                    }.buttonStyle(.plain).fontWeight(selected == 4 ? .semibold : .regular)
                        .background(selected == 4 ? Color.accentColor.opacity(0.13) : .clear, in: RoundedRectangle(cornerRadius: 9))
                    Divider()
                    Text("PAGES").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
                    Text("Order follows your swipes.").font(.caption).foregroundStyle(.secondary)
                    ForEach(Array(model.draft.order.enumerated()), id: \.element) { index, id in
                        let page = KnobPage(rawValue: id)!
                        HStack(spacing: 8) {
                            Button { selected = id } label: {
                                Label(page.name, systemImage: page.icon).frame(maxWidth: .infinity, alignment: .leading)
                                    .contentShape(Rectangle())
                            }.buttonStyle(.plain).fontWeight(selected == id ? .semibold : .regular)
                            if index < model.draft.count {
                                Button { model.draft.order.swapAt(index,index-1) } label: { Image(systemName: "chevron.up") }
                                    .disabled(index == 0).help("Move earlier")
                                Button { model.draft.order.swapAt(index,index+1) } label: { Image(systemName: "chevron.down") }
                                    .disabled(index == model.draft.count-1).help("Move later")
                            } else { Text("Off").font(.caption).foregroundStyle(.secondary) }
                        }.buttonStyle(.borderless).padding(10)
                            .background(selected == id ? Color.accentColor.opacity(0.13) : .clear, in: RoundedRectangle(cornerRadius: 9))
                            .contentShape(Rectangle())
                            .onTapGesture { selected = id }
                    }
                    Spacer()
                    Label("\(model.currentPage) on knob",systemImage: "circle.dotted").font(.caption)
                    Label(model.hidReady ? "Keyboard HID ready" : "Keyboard HID not ready",systemImage: "keyboard").font(.caption).foregroundStyle(.secondary)
                    Label(model.awake ? "Display awake" : "Display sleeping",systemImage: model.awake ? "sun.max" : "moon").font(.caption).foregroundStyle(.secondary)
                }.padding(20).frame(minWidth: 225, idealWidth: 240, maxWidth: 260)
                ScrollView {
                    VStack(alignment: .leading, spacing: 20) {
                        if selected == 4 {
                            Label("General", systemImage: "gearshape").font(.title.bold())
                            generalSettings
                            diagnostics
                        } else { pageEditor }
                    }.padding(24)
                }.frame(minWidth: 480)
            }.disabled(model.applying)
            Divider()
            HStack {
                VStack(alignment: .leading,spacing: 3) {
                    Text(model.applying ? "Saving…" : model.dirty ? "Changes ready to apply" : "Configuration is up to date").font(.callout.weight(.medium))
                    Text(model.message).font(.caption).foregroundStyle(.secondary).lineLimit(2).textSelection(.enabled)
                }
                Spacer()
                Button("Revert") { model.revert() }.disabled(!model.dirty || model.applying || model.deviceConfig == nil)
                Button("Apply to knob") { model.apply() }.buttonStyle(.borderedProminent)
                    .disabled(!model.ready || !model.dirty || model.applying).keyboardShortcut("s", modifiers: .command)
            }.padding(18)
        }
    }
    var pageEditor: some View {
        let page = KnobPage(rawValue: selected)!
        return VStack(alignment: .leading, spacing: 16) {
            Label(page.name,systemImage: page.icon).font(.title.bold())
            Toggle("Include in page rotation",isOn: Binding(get: { model.draft.order.prefix(model.draft.count).contains(selected) },set: { model.draft.setEnabled(selected,$0) }))
                .disabled(model.draft.count == 1 && model.draft.order[0] == selected)
            GroupBox {
                VStack(alignment: .leading,spacing: 18) {
                    if page.bounded {
                        HStack {
                            Text("Change per notch")
                            Spacer()
                            HStack(spacing: 2) {
                                stepOption("Low", step: 2)
                                stepOption("High", step: 4)
                            }.padding(3)
                                .background(.quaternary, in: Capsule())
                        }
                        Text("Low: 2% per notch · High: 4% per notch")
                            .font(.caption).foregroundStyle(.secondary)
                        Text("\(100/model.draft.pages[selected].step + 1) positions, including 0% and 100%.").font(.caption).foregroundStyle(.secondary)
                        intSlider("Travel",value: $model.draft.pages[selected].arc,range: 90...300,suffix: "°")
                        ArcPreview(arc: model.draft.pages[selected].arc, step: model.draft.pages[selected].step)
                            .frame(height: 120)
                        Text("The gap separates the endpoints. The knob returns to the Mac’s current value when this page opens.").font(.caption).foregroundStyle(.secondary)
                    } else if page == .media {
                        Stepper("\(model.draft.pages[selected].notches) notches per revolution",value: $model.draft.pages[selected].notches,in: 3...100)
                    } else {
                        Text("Three fixed positions: unmute, mute and deafen. Tap to disconnect.").font(.callout)
                    }
                    intSlider("Detent strength",value: $model.draft.pages[selected].strength,range: 0...80,suffix: "", display: { "\(Int((Double($0)/80*100).rounded()))%" })
                    intSlider("Detent sharpness",value: Binding(
                        get: { model.draft.pages[selected].buildup / 2 },
                        set: { model.draft.pages[selected].buildup = max(1, $0 * 2) }
                    ),range: 0...100,suffix: "%")
                    if page.bounded { Text("Endpoint resistance uses the same strength. These are motor-generated stops; sufficient hand force can overcome them.").font(.caption).foregroundStyle(.secondary) }
                }.padding(12)
            }
            if page == .discord {
                GroupBox("Keyboard shortcuts") {
                    VStack(alignment: .leading, spacing: 14) {
                        ShortcutEditor(title: "Toggle mute",shortcut: $model.draft.shortcuts[0])
                        ShortcutEditor(title: "Toggle deafen",shortcut: $model.draft.shortcuts[1])
                        ShortcutEditor(title: "Disconnect",shortcut: $model.draft.shortcuts[2])
                        Text("Set the same shortcuts in Discord. Position is remembered locally; Discord does not report its mute state through HID.").font(.caption).foregroundStyle(.secondary)
                    }.padding(10)
                }
            }
            if page == .volume {
                capability("Default audio output",value: model.volume)
            }
            if page == .brightness {
                Picker("Display",selection: $model.displayID) {
                    Text("Main display").tag(UInt32(0))
                    ForEach(model.displays,id: \.0) { Text($0.1).tag($0.0) }
                }
                capability("Display brightness",value: model.brightness)
                Text("Brightness support varies by display. An unsupported display needs an additional integration; its percentage is never estimated from key presses.").font(.caption).foregroundStyle(.secondary)
            }
            if page == .media {
                Picker("Control through",selection: $model.mediaTarget) {
                    ForEach(MediaTarget.allCases,id: \.self) { Text($0.rawValue).tag($0) }
                }
                Text("System media keys use the knob’s HID connection. Music and Spotify are controlled by the app and may request Automation permission. Keep the selected player open.").font(.caption).foregroundStyle(.secondary)
            }
        }
    }
    var generalSettings: some View {
        GroupBox("General") {
            VStack(alignment: .leading, spacing: 14) {
                Stepper(model.draft.sleepMinutes == 0 ? "Display sleep: never" : "Display sleep: after \(model.draft.sleepMinutes) min",value: $model.draft.sleepMinutes,in: 0...120)
                Text("Sleeping turns off the display and motor torque. Turn or touch to wake.").font(.caption).foregroundStyle(.secondary)
                Toggle("Launch app at login",isOn: $ui.login).onChange(of: login) { _, enabled in
                    do { if enabled { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() } }
                    catch { model.log("Login item: \(error.localizedDescription)"); login = SMAppService.mainApp.status == .enabled }
                }
                Text("Display and media choices apply immediately on this Mac. Apply saves page and sleep settings on the knob.").font(.caption).foregroundStyle(.secondary)
            }.padding(10)
        }
    }
    var diagnostics: some View {
        GroupBox("Connection & diagnostics") {
            VStack(alignment: .leading,spacing: 10) {
                HStack {
                    Button("Scan") { model.scan() }.disabled(model.ready)
                    if model.ready { Button("Disconnect") { model.disconnect() } }
                    Spacer()
                    if let config = model.deviceConfig { Text("Config v1 · \(String(config.hash,radix:16))").font(.caption.monospaced()).foregroundStyle(.secondary) }
                }
                if !model.ready {
                    ForEach(model.devices) { item in
                        HStack { Text(item.name); Spacer(); Button("Connect") { model.connect(item.peripheral) } }
                    }
                }
                DisclosureGroup("Recent activity") {
                    VStack(alignment: .leading,spacing: 5) {
                        ForEach(Array(model.logs.prefix(20).enumerated()),id: \.offset) { _, text in Text(text).font(.caption.monospaced()).textSelection(.enabled) }
                    }.frame(maxWidth: .infinity,alignment: .leading).padding(.top,8)
                }
            }.padding(10)
        }
    }
    func stepOption(_ title: String, step: Int) -> some View {
        let active = model.draft.pages[selected].step == step
        return Button { model.draft.pages[selected].step = step } label: {
            Text(title).fontWeight(active ? .semibold : .regular)
                .frame(width: 62).padding(.vertical, 6)
                .foregroundStyle(active ? Color.white : Color.primary)
                .background(active ? Color.accentColor : .clear, in: Capsule())
                .contentShape(Capsule())
        }.buttonStyle(.plain)
            .accessibilityLabel("\(title), \(step)% per notch")
            .accessibilityAddTraits(active ? .isSelected : [])
    }
    func capability(_ title: String,value: Double?) -> some View {
        HStack { Text(title); Spacer(); Text(value.map { "\(Int(($0*100).rounded()))%" } ?? "Unavailable").foregroundStyle(value == nil ? .orange : .secondary) }
    }
    func intSlider(_ label: String,value: Binding<Int>,range: ClosedRange<Int>,suffix: String,display: ((Int)->String)? = nil) -> some View {
        VStack(spacing: 6) {
            HStack { Text(label); Spacer(); Text(display?(value.wrappedValue) ?? "\(value.wrappedValue)\(suffix)").foregroundStyle(.secondary).monospacedDigit() }
            Slider(value: Binding(get: { Double(value.wrappedValue) },set: { value.wrappedValue = Int($0.rounded()) }),in: Double(range.lowerBound)...Double(range.upperBound),step: 1)
        }
    }
}
struct ArcPreview: View {
    let arc: Int, step: Int
    var body: some View {
        Canvas { ctx,size in
            let c = CGPoint(x: size.width/2,y: size.height/2+5), r: Double = 49
            let start = -Double.pi/2-Double(arc)*Double.pi/360
            for i in 0...100/step {
                let a = start+Double(i*step)/100*Double(arc)*Double.pi/180
                var line = Path()
                line.move(to: CGPoint(x: c.x+cos(a)*(r-6),y: c.y+sin(a)*(r-6)))
                line.addLine(to: CGPoint(x: c.x+cos(a)*r,y: c.y+sin(a)*r))
                ctx.stroke(line,with: .color(.accentColor),lineWidth: i == 0 || i == 100/step ? 3 : 1)
            }
            ctx.draw(Text("0–100%").font(.caption.bold()),at: c)
        }.accessibilityLabel("\(arc) degree arc, \(100/step+1) positions")
    }
}
struct ShortcutEditor: View {
    let title: String
    @Binding var shortcut: Shortcut
    static let keys: [(Int,String)] = (0..<26).map { (4+$0,String(UnicodeScalar(65+$0)!)) } +
        (0..<9).map { (30+$0,String($0+1)) } + [(39,"0"),(40,"Return"),(41,"Escape"),(42,"Backspace"),(43,"Tab"),(44,"Space"),(79,"Right"),(80,"Left"),(81,"Down"),(82,"Up")] +
        (0..<12).map { (58+$0,"F\($0+1)") } + (0..<12).map { (104+$0,"F\($0+13)") }
    var body: some View {
        VStack(alignment: .leading,spacing: 6) {
            Text(title).font(.callout.weight(.medium))
            HStack {
                ForEach([(1,"⌃"),(2,"⇧"),(4,"⌥"),(8,"⌘")],id: \.0) { bit,label in
                    Toggle(label,isOn: Binding(get: { shortcut.modifiers & bit != 0 },set: { on in if on { shortcut.modifiers |= bit } else { shortcut.modifiers &= ~bit } })).toggleStyle(.button)
                }
                Picker("Key",selection: $shortcut.key) { ForEach(Self.keys,id: \.0) { Text($0.1).tag($0.0) } }.labelsHidden()
            }
        }
    }
}
