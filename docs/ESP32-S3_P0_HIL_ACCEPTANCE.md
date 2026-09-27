# GuangHeng Energy Companion P0

## ESP32-S3 Hardware-in-the-loop Acceptance

Date: 2026-09-25  
Board target: Waveshare ESP32-S3-Touch-AMOLED-1.8  
MCU target: ESP32-S3  
Firmware: ESP-IDF 5.5.5  
Backend: https://43.155.204.194  
Backend version: 1.5.0

## Current result

Status: **PARTIAL — BLE END-TO-END PHONE CONFIRMATION REQUIRED**

The real ESP32-S3 board has passed identity, flash, PSRAM, AMOLED, touch, IMU,
PMU, RTC, ES8311, board-microphone, board-speaker, Wi-Fi, trustworthy-time,
and production TLS checks. The production revocation path has also been exercised:
the authenticated snapshot returned HTTP 401 after authorization was revoked, the
firmware erased only the revoked credential, preserved identity and Wi-Fi, and then
obtained a new short-lived device pairing session from the production backend.
The pairing screen uses full Simplified Chinese glyph coverage and a dedicated,
unobstructed six-digit code area. Unsupported decorative glyphs that appeared as
empty squares were removed from the production strings; Chinese text itself remains
unchanged. ESP32-S3 encrypted BLE advertising is active on the real board. The
Flutter client and ESP32-S3 NVS/Wi-Fi path build successfully, but a real phone has
not yet completed the credential write and Wi-Fi transition, so BLE end-to-end is
not marked complete.

TMAG5273 requires soldering on this hardware and is explicitly deferred to P2
by the project owner. It is not reported as completed or passed.

## Evidence matrix

| Gate | Status | Evidence |
|---|---|---|
| ESP32-S3 USB/serial detection | PASS | COM10, USB Serial/JTAG, VID/PID `303A:1001` |
| ESP32-S3 chip model | PASS | esptool and boot log: ESP32-S3 QFN56 |
| ESP32-S3 chip revision | PASS | v0.2 |
| Flash size | PASS | Runtime and esptool: 16 MB |
| PSRAM | PASS | 8 MB Octal PSRAM at 80 MHz; runtime memory test OK |
| MAC | PASS | `90:70:69:FE:A9:3C` |
| Physical board revision | PENDING | CST816S proves V2-family signal; PCB label/photo still required |
| ESP-IDF target | PASS | `esp32s3` |
| ESP32-S3 clean build | PASS | `fullclean` followed by successful build |
| ESP32-S3 flash | PASS | All written image hashes verified on COM10 |
| AMOLED | PASS | CO5300 initialized; first-frame log plus user visual confirmation |
| Touch | PASS | CST816S real pressed events observed repeatedly |
| QMI8658 | PASS | WHO_AM_I `0x05`; real acceleration, gyro, temperature, timestamp read |
| AXP2101 | PASS | Address `0x34`, chip ID `0x4A`, read-only HIL |
| PCF85063A | PASS | Address `0x51`; two reads 1.1 seconds apart proved ticking |
| ES8311 | PASS | Address `0x30`; 16 kHz full-duplex codec opened |
| Board microphone | PASS | 16,128 real samples; span 64,308; non-constant signal |
| Board speaker | PASS | 32,256 bytes of 880 Hz PCM written; user confirmed audible output |
| TMAG5273 | DEFERRED TO P2 | Requires soldering; no GPIO or I2C wiring guessed |
| Wi-Fi | PASS | WPA2-PSK association; DHCP IPv4 `192.168.2.8`; reconnect handler enabled |
| SNTP trustworthy time | PASS | Secure gate logged UTC `2026-09-25T08:07:44Z` before HTTPS |
| HTTPS/TLS | PASS | Production certificate bundle validation succeeded; CN checking enabled; no insecure TLS flag |
| Pairing session creation | PASS | Production API returned HTTP 200; short-lived code displayed on AMOLED; no credential logged |
| User pairing confirmation | PASS | Existing user-approved device credential loaded from ESP32-S3 NVS |
| Authenticated backend snapshot | PASS | Repeated production `GET /api/v1/companion/snapshot` responses returned HTTP 200 after certificate validation |
| Authorization revocation recovery | PASS | Production snapshot returned HTTP 401; firmware erased only NVS `credential`, preserved device identity/Wi-Fi, and automatically requested a new pairing session |
| Chinese watch UI build and flash | PASS | Full Source Han Sans SC CJK ranges are embedded at 14 px and 16 px; firmware built and all flashed image hashes verified |
| Special-glyph visual correction | PASS | Unsupported bullet and middle-dot glyphs were replaced with font-safe text; latest firmware build contains no affected strings |
| Six-digit pairing layout visual check | PASS | User photo confirms a real short-lived code in six dedicated digit cells; no code is copied into this report or firmware logs |
| AMOLED redraw memory stability | PASS | BSP LVGL transfer buffer reduced to 20 lines; repeated TLS and pairing polling produced no SPI DMA allocation errors |
| Flutter pairing sheet lifecycle | PASS | Controller lifetime moved into the sheet State; targeted `flutter analyze` reports no issues |
| ESP32-S3 encrypted BLE advertising | PASS | Real boot log: encrypted provisioning service started and advertised; MITM, LE Secure Connections, bonding, and authenticated GATT access enabled |
| Flutter BLE provisioning build | PASS | Android debug APK builds with BLE scan/connect permissions and Chinese onboarding UI |
| BLE credentials -> ESP32-S3 NVS -> Wi-Fi | PENDING | Implementation is complete, but no real phone credential write and network transition has yet been observed; current HIL still uses local build fallback |

## Current build artifact

- Binary: `build/guangheng_energy_companion.bin`
- Size: `3186944` bytes (`0x30A100`)
- Smallest app partition: `0x600000` bytes
- Free application space: 49%
- SHA-256: `BC56ED45CE2132B945CB9C3AECF01AFB2A63695DAEC93520ABA011B29FEC78A9`

## Flutter HIL artifact

- Android APK: `D:\flutterProject\guangheng\build\app\outputs\flutter-apk\app-debug.apk`
- Size: `177611046` bytes
- SHA-256: `210F4514DA13C2E25B5CD8A8BCD7FE167B4C79101A6F65C307F7477BA2D1B756`
- Static analysis: PASS
- Flutter tests: PASS (`15` tests)
- Android debug build: PASS
- Real Android BLE provisioning: PENDING (phone was not connected to this workstation)
- Real iOS BLE provisioning: PENDING (requires Apple hardware)

## Safety and truthfulness notes

1. The firmware rejects any detected MCU other than ESP32-S3.
2. PSRAM acceptance comes from runtime detection and memory test, not board specs.
3. AXP2101 HIL is read-only and does not alter PMU rails or charging behavior.
4. PCF85063A is not assigned a fabricated date; only oscillator progress is accepted.
5. The microphone is accepted from real varying PCM samples, not codec init alone.
6. Speaker acceptance includes both successful PCM delivery and user audible confirmation.
7. TMAG5273 is deferred, not bypassed and not marked complete.
8. Production HTTPS cannot run before the SNTP trustworthy-time gate passes.
9. Certificate verification remains enabled; insecure TLS is not configured.
10. SSID and Wi-Fi password are excluded from this report and from HIL logs.
11. Only an explicit production HTTP 401/403 revokes the local credential;
    transient network, TLS, timeout, and server failures never erase authorization.
12. Revocation erases only the authorization credential and does not erase device
    identity or Wi-Fi configuration.
13. Compile-time Wi-Fi configuration remains a HIL-only fallback. Authenticated
    BLE onboarding from Flutter now persists Wi-Fi credentials in ESP32-S3 NVS,
    but real phone end-to-end acceptance is still pending.
14. The production API runs image `guangheng-server:1.6.0` while preserving its
    externally reported Backend Version `1.5.0`; MCP remains unchanged on image
    `1.5.0` with exactly 14 allowed tools.
15. Backend regression is `140 passed`; production health, energy state,
    household energy graph, device registry, strategy, tariff, decision context,
    and Companion device-list endpoints returned HTTP 200.

## Next gate

1. Install the current Flutter Android build on a real phone and complete encrypted
   BLE Wi-Fi provisioning while observing ESP32-S3 NVS storage and Wi-Fi transition.
2. Enter the real short-lived six-digit code in Flutter and confirm the screen
   returns to the authenticated dashboard without logging or persisting the code.
3. Validate WebSocket connection and reconnect behavior.
4. Complete iOS BLE onboarding acceptance on real Apple hardware.
