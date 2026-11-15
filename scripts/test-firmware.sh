#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")/.."
for test in ble_lifecycle input_actions touch_input config_and_bounds haptics; do
  c++ -std=c++17 -Itests/ble_fakes "tests/$test.cpp" -o "/tmp/smartknob-$test"
  "/tmp/smartknob-$test"
done
