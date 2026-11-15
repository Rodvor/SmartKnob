#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")/.."
swiftc -module-cache-path /tmp/smartknob-swift-test-cache macOS/SmartKnob/Sources/SmartKnob/Configuration.swift tests/ConfigChecks.swift -o /tmp/smartknob-config-tests
/tmp/smartknob-config-tests
