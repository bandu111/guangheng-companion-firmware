# GuangHeng Energy Companion G2 Firmware Package

- Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.8
- MCU: ESP32-S3
- ESP-IDF: 5.5.5
- Build type: independent clean full build
- Target: `esp32s3`
- Toolchain: `esp-14.2.0_20260121`
- Date: 2026-09-26

## Images

| File | Offset | Bytes | SHA-256 |
|---|---:|---:|---|
| `guangheng_energy_companion_g2_factory.bin` | `0x0` | 3334304 | `85BE2D3A726882E046B8F7881540A54D003779638E1F60EC04CC7E5DED7EA087` |
| `bootloader.bin` | `0x0` | 20832 | `7CDCCA8CD8328C471D9000A1A9A85AC4E21151DB7E3FDAF6668DFFEE6504EBCD` |
| `partition-table.bin` | `0x8000` | 3072 | `23AA1DC43044918F5F4768DD44813ECBB44A288996EB729EC5DB610413212735` |
| `ota_data_initial.bin` | `0xF000` | 8192 | `7D2C7AC4888BFD75CD5F56E8D61F69595121183AFC81556C876732FD3782C62F` |
| `guangheng_energy_companion.bin` | `0x20000` | 3203232 | `94E7BA2800979D86D8D73F897E5CAD77ECC82E7AE48EEEF49A48C08F41BEEA10` |

The merged factory image contains the bootloader, partition table, initial OTA
data, and application at their verified offsets. `flash_args.txt` preserves the
multi-image ESP-IDF flashing layout.

This package is a G2 test artifact. It does not claim G2 PASS until real
ESP32-S3 long-press approval and Backend state transition evidence pass.
