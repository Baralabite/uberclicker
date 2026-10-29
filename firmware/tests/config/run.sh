#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
json=firmware/.pio/libdeps/thinknode_m5/ArduinoJson/src
if [ ! -d "$json" ]; then echo 'Run pio run -d firmware first to install pinned ArduinoJson'; exit 1; fi
if command -v pkg-config >/dev/null && pkg-config --exists openssl; then
  ssl_flags="$(pkg-config --cflags --libs openssl)"
else
  ssl="$(brew --prefix openssl@3)"
  ssl_flags="-I$ssl/include -L$ssl/lib -lcrypto"
fi
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# Intentional flag splitting: pkg-config emits compiler arguments.
c++ -std=c++17 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -DCONFIG_NIMBLE_ENABLED=1 -DUC_CONFIG_HOST_TEST=1 -Ifirmware/tests/config/stubs -Ifirmware/include -I"$json" firmware/src/config_service.cpp firmware/tests/config/service.cpp $ssl_flags -o "$work/service"
"$work/service"
c++ -std=c++17 firmware/tests/config/framing.cpp -o "$work/framing"
"$work/framing"
