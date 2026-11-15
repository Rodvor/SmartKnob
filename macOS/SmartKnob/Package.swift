// swift-tools-version: 5.9
import PackageDescription
let package = Package(name: "SmartKnob", platforms: [.macOS(.v14)], products: [.executable(name: "SmartKnob", targets: ["SmartKnob"])], targets: [.executableTarget(name: "SmartKnob")])
