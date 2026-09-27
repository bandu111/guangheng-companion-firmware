# GuangHeng Energy Companion ESP32-S3 Firmware

ESP-IDF firmware for the portable GuangHeng Energy Companion described by the
V1.1 development specification.

The fixed hardware target is the **Waveshare ESP32-S3-Touch-AMOLED-1.8** with an
**ESP32-S3** MCU. Project target settings are not treated as proof of attached
hardware: P0 acceptance requires an esptool/serial boot log from the real board.

## Current milestone

- Official Waveshare managed BSP for `ESP32-S3-Touch-AMOLED-1.8`
- AMOLED/touch LVGL shell shared by original and V2 boards
- Ambient energy dashboard with an explicit backend-reported source label
- Cross-device proposal card
- 1.2–1.5 second touch hold required for approval
- Local approval precondition guard
- Separate household storage SOC and companion battery values
- TMAG5273 configuration boundary without guessed pins
- Network boundary that only permits a GuangHeng Companion API
- No Home Assistant, Hermes, MCP, SOLIX, SSH, or model-provider secret storage

The firmware contains no local energy or Proposal fixture path. Energy state,
pairing, approval challenges, and Action Sets must come from GuangHeng Backend.

## App and backend linkage

1. Configure Wi-Fi and the GuangHeng API endpoint with `idf.py menuconfig`.
2. On first boot the device requests and displays a six-digit pairing code.
3. In Flutter open **设备 → 光衡随身终端**, enter that code, and confirm.
4. The device receives a revocable device credential and stores it in NVS.
5. Energy snapshots and pending cross-device Action Sets come from the same
   backend used by Flutter.
6. A long touch sends a single-use, version-bound approval challenge back to the
   backend. Existing Safety, Execution, and Smart Meter verification remain the
   only execution path.

The production endpoint is `https://43.155.204.194`. ESP32-S3 firmware waits for
SNTP time synchronization and validates the server certificate with the ESP-IDF
certificate bundle before sending production requests. Development HTTP remains
an explicit, disabled-by-default menuconfig option.

## Toolchain

The official managed BSP currently requires ESP-IDF 5.5.x or 6.0.x. ESP-IDF
5.5.5 is the recommended baseline.

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

## Required hardware confirmation

Before enabling external sensors or calling the build hardware-verified, record:

1. ESP32-S3 chip model, chip revision, flash size, PSRAM size, MAC, and COM port.
2. Whether Windows enumerates USB Serial/JTAG or a USB-UART bridge.
3. The board-back revision label (original SH8601/FT3168 or V2 CO5300/CST820).
4. The BSP touch-controller probe used only as a board-variant signal.
5. The exact TMAG5273 part/module and observed I2C address.
6. The BSP I2C scan result and the selected external SDA/SCL pins.

Do not assume GPIO14/GPIO15 from community examples.

Every runtime report uses the prefix `ESP32-S3 Hardware-in-the-loop`. A build
artifact alone must never be reported as ESP32-S3 hardware acceptance.

## Production gates

Approval remains blocked until the backend provides a device-bound Companion
pairing/session API with proposal revision/hash, nonce, timestamp, expiry, and
replay protection.
# guangheng-backend
