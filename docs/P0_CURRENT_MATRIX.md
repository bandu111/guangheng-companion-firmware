# GuangHeng Energy Companion — P0 CURRENT MATRIX (STEP 1 Audit)

Date: 2026-09-25
Scope: P0 audit only. No code was modified in this step.
Basis: GuangHeng_Energy_Companion_Development_Spec_V1.1.pdf + latest human decisions (TMAG5273 / Magnet / Magnetic Context / Phone-Dock differentiation all deferred to P2; they do not affect P0/P1 verdicts).

## Audit metadata

| Item | Value | Evidence |
|---|---|---|
| Firmware root | `D:\ESPProject\guangheng_energy_companion` | — |
| Firmware binary | `build/guangheng_energy_companion.bin`, 3,186,944 bytes, rebuilt 2026-09-25 21:14 (local time) | Filesystem |
| Git commit | NONE — project is not a git repository | `git log` empty |
| ESP-IDF | 5.5.5 | `dependencies.lock` |
| Target | `esp32s3` (compile-time guard rejects other MCUs) | `main/gh_hardware.c` |
| Board revision | UNKNOWN (CST816S V2-family signal; physical PCB label still not confirmed) | `gh_hardware_report_board_variant()` |
| Backend | `https://43.155.204.194`, live probe today: `/health` → `{"status":"ok","service":"GuangHeng Server","version":"1.5.0","environment":"production"}`; unauth/bad-credential snapshot → HTTP 401 | curl 2026-09-25 |
| Backend source | Not present on this workstation (deployed server image only); backend pytest evidence is from the prior acceptance record (140 passed) | — |
| Firmware host unit tests | `gh_model_tests.exe`: **5/5 PASS** (executed during this audit) | build-host run |
| Flutter | `D:\flutterProject\guangheng`; pairing confirm + device list/revoke wired to same backend; prior record: 15 tests PASS, analyze clean | source inspection |
| MCP | 127.0.0.1:8001 not running locally during this audit; 14-tool boundary claimed by prior acceptance record only — not re-verified this session | curl empty |

Legend: PASS / PARTIAL / FAIL / NOT IMPLEMENTED / HARDWARE UNVERIFIED / BLOCKED EXTERNAL / DEFERRED TO P2.

---

## P0 CURRENT MATRIX

### A. Board & core hardware

| # | Feature | Expected | Current Implementation | Runtime Evidence | Hardware Evidence | Fake Data Risk | Verdict |
|---|---|---|---|---|---|---|---|
| A1 | ESP32-S3 identity, 16 MB Flash, 8 MB PSRAM | Real chip detect at runtime; reject non-S3 | `gh_hardware_verify_esp32s3()` runs at every boot; chip model/revision/flash/MAC/PSRAM logged; hard stop on wrong MCU or no PSRAM | Prior HIL log: ESP32-S3 QFN56 v0.2, 16 MB flash, 8 MB octal PSRAM, MAC `90:70:69:FE:A9:3C` | COM10 USB Serial/JTAG `303A:1001`; image hashes verified | None | **PASS** |
| A2 | AMOLED (CO5300) | Real display output | BSP LVGL shell, brightness set, first-frame log | Prior HIL: first frame + user visual confirmation | Same | None | **PASS** |
| A3 | Touch (CST816S) | Real touch events driving UI | LVGL press callback logs real presses; press/release also drive approval hold logic | Prior HIL: repeated real press events | Same | None | **PASS** |
| A4 | QMI8658 IMU (HIL read) | Real accel/gyro read | One-shot boot HIL: WHO_AM_I 0x05, configure 4G/250Hz, read real data | Prior HIL log with real accel/gyro/temp/timestamp | Same | None | **PASS** |
| A5 | QMI8658 runtime service + Lift event | Continuous sampling, real lift detection → Explain | **No runtime IMU task.** `gh_model_set_lifted()` exists but is never called anywhere (dead code). QMI8658 is only read once during boot HIL | None | None | None | **FAIL (Lift-to-Explain NOT IMPLEMENTED)** |
| A6 | AXP2101 (HIL) | Real PMU presence | Boot HIL: probe 0x34, chip ID 0x4A, read-only | Prior HIL log | Same | None | **PASS** |
| A7 | Companion Battery (runtime) | Real AXP2101 battery % or voltage, or "--"; never a fabricated number | **No runtime PMU read.** `companion_battery_pct` is never assigned after `memset(0)`. UI renders "终端电量 0%" — a fabricated value | Display shows constant 0% | None | **FAKE PRODUCTION DATA — hardcoded zero displayed as battery** | **FAIL** |
| A8 | PCF85063 RTC | Real ticking RTC | Boot HIL: two reads 1.1 s apart prove oscillator | Prior HIL log | Same | None (no fabricated date assigned) | **PASS** |
| A9 | ES8311 + I2S + Board Mic PCM + Speaker | Real codec, real PCM in, audible out | Boot HIL only: ES8311 opened full-duplex 16 kHz; mic capture 16,128 samples with span ≥ 32 enforced; 880 Hz tone to speaker | Prior HIL: real mic samples, audible tone confirmed by user | Same | Test tone constants are TEST ONLY (classification OK) | **PASS (as hardware gate)** |
| A10 | Board revision label | UNKNOWN until physically confirmed | Reports UNVERIFIED; CST816S V2-family signal only | Prior HIL log | PCB label/photo still missing | None | **HARDWARE UNVERIFIED (by design — do not guess)** |
| A11 | TMAG5273 / Magnet / Magnetic Context | Deferred | `gh_hall.c` is a disabled stub returning `ESP_ERR_NOT_SUPPORTED`; no fake Bx/By/Bz, no simulated attach state; `magnet_attached` in model is never displayed | None | None | None (interface boundary only, no fake data) | **DEFERRED TO P2** |

### B. Network / Security

| # | Feature | Expected | Current Implementation | Runtime Evidence | Hardware Evidence | Fake Data Risk | Verdict |
|---|---|---|---|---|---|---|---|
| B1 | Wi-Fi real connection + reconnect | WPA2 STA, DHCP, auto-reconnect | STA mode, WPA2 threshold, disconnect handler calls `esp_wifi_connect()`; BLE-provisioned credentials from NVS with compile-time HIL fallback | Prior HIL: association + IPv4 `192.168.2.8` | Same | Wi-Fi PSK exists in `sdkconfig` (HIL fallback — see Secret Audit) | **PASS** |
| B2 | SNTP + trustworthy time before TLS | No TLS before valid time | `ensure_secure_network_ready()`: 5 s loop; SNTP starts only for https; trustworthy-time gate (≥2024-01-01) blocks HTTPS; logs UTC once | Prior HIL: `2026-09-25T08:07:44Z` logged before HTTPS | — | None | **PASS** |
| B3 | HTTPS + CA bundle + SAN/IP verification | Production `https://43.155.204.194`, cert bundle, CN check on, no insecure TLS | `esp_crt_bundle_attach`, `skip_cert_common_name_check = false`; `GH_ALLOW_INSECURE_HTTP` defaults n and is unset in sdkconfig; transport gate refuses non-https unless explicit opt-in | Prior HIL: production TLS validation passed; today's live probe answered over HTTPS with real cert validation (curl -k used only for PC-side probing, not firmware) | — | None | **PASS** |
| B4 | Backend reconnect | Retry forever | 5 s refresh loop + Wi-Fi reconnect | Prior HIL + live server today | — | None | **PASS** |
| B5 | Offline detection | Detect backend/wifi loss | Refresh failure → `gh_model_set_offline()` (backend_online=false, approval blocked) | Prior HIL revocation path | — | None | **PARTIAL** |
| B6 | Stale / Last Sync display | "上次同步 xx:xx"; stale data marked STALE | **Not implemented.** `observed_at_ms` field exists but never parsed or stored; no last-sync UI; after a failure the previous numbers simply freeze with an offline label, values not marked stale | None | — | Frozen values shown without stale marking | **FAIL** |
| B7 | WebSocket (WSS) | Real WSS or explicitly recorded REST architecture change | **No WSS.** `websocket_online` hardcoded false; design is REST polling every 5 s | Architecture fact | — | n/a | **NOT IMPLEMENTED** — decision required: implement WSS or record REST polling as architecture change |

### C. Pairing

| # | Feature | Expected | Current Implementation | Runtime Evidence | Hardware Evidence | Fake Data Risk | Verdict |
|---|---|---|---|---|---|---|---|
| C1 | First-boot unpaired state | Device requests real code | If no NVS credential → POST `/api/v1/companion/pairing/device-code` with device_uid; code + poll_token stored in RAM | Prior HIL: HTTP 200, short-lived code on AMOLED | Real screen photo (prior doc) | No fixed code | **PASS** |
| C2 | Display real 6-digit code | Dedicated digits on device | Pairing page with 6 dedicated digit cells, Chinese-only product copy | Prior HIL + photo | Same | None | **PASS** |
| C3 | Flutter input/confirm | App enters code, backend confirms | Flutter `device_page.dart`: pairing sheet, 6-digit entry, POST `/api/v1/companion/pairing/confirm` | Source inspection + prior analyze/test PASS | — | None | **PASS** |
| C4 | Device claims credential → NVS | Poll endpoint until PAIRED | `/pairing/poll` with code+poll_token; on PAIRED saves device_id+credential to NVS `gh_companion` | Prior HIL: credential stored, not logged | — | Credential never printed | **PASS** |
| C5 | Reboot persistence | Credential survives reboot | `load_credential()` at boot | Prior HIL: user-approved credential reloaded after reboot | — | None | **PASS** |
| C6 | Revocation | 401/403 → erase credential only, keep identity+Wi-Fi, re-pair | Only explicit 401/403 clears NVS `credential`; transient errors never erase | Prior HIL: revocation exercised end-to-end | — | None | **PASS** |
| C7 | Code expiry / one-time | Backend-enforced | Backend-side; prior backend regression (140 tests) covers pairing; firmware re-requests new code after revoke | Prior record (backend source not on this workstation to re-run) | — | None | **PASS (BLOCKED EXTERNAL for local re-verification)** |
| C8 | No fixed pairing code | No compile-time code | None in firmware; code always from backend | Source scan | — | None | **PASS** |
| C9 | Pairing/Voice separation | Pairing page shows pairing only | Pairing page contains only: brand, 安全连接, code, 等待手机确认, privacy note. No voice/Hermes/energy-control elements (no voice UI exists anywhere yet) | Source inspection | — | None | **PASS (trivially — voice UI does not exist)** |

### D. Ambient Energy / Energy Clock

| # | Feature | Expected | Current Implementation | Runtime Evidence | Hardware Evidence | Fake Data Risk | Verdict |
|---|---|---|---|---|---|---|---|
| D1 | PV / Home Load / Grid / Storage SOC from backend | Real backend snapshot values | `GET /api/v1/companion/snapshot` parsed: solar_w, home_load_w, grid_import/export_w, soc_percent | Prior HIL: authenticated snapshot HTTP 200 repeatedly; backend live today | — | Values come from backend (simulator server is allowed as "real server simulator data") | **PASS** |
| D2 | Agent state from backend | Reflects backend agent state | Derived locally from pending_action_set presence (MONITORING/PENDING_APPROVAL); not a real backend agent-state field | — | — | Locally synthesized state label | **PARTIAL** |
| D3 | source_mode | Must show 模拟数据/Simulator when backend is simulator | **`model->energy.source_mode = GH_SOURCE_REAL` is hardcoded** in `gh_backend_refresh()` and never read from the response. Backend exposes `source_mode` (Flutter parses `'simulator'`), device ignores it and displays "实时数据" | Flutter source proves backend field exists; firmware ignores it | — | **CRITICAL: UI claims REAL data while backend is Simulator — truthfulness violation** | **FAIL** |
| D4 | last_sync | Show last sync time | Not parsed, not displayed | None | — | None | **FAIL** |

### E. UI (screen inventory & style)

| # | Feature | Expected | Current Implementation | Runtime Evidence | Hardware Evidence | Fake Data Risk | Verdict |
|---|---|---|---|---|---|---|---|
| E1 | Boot screen | 光衡 / Energy Companion | No boot screen; first render is the dashboard | — | — | None | **NOT IMPLEMENTED** |
| E2 | Connecting / Time / Backend screens | 连接 Wi-Fi / 同步时间 / 连接光衡 | No separate screens; a single status label shows "正在连接 / 同步时间 / 安全连接 / 离线" text inside the energy card | — | — | None | **PARTIAL (state text exists, screens do not)** |
| E3 | Ambient screen (Apple-Watch style: central metric, bottom mini-cards) | One main task per page | Single dense dashboard: "家庭能源" card with 4-line text block (光伏/家庭/电网/储能/终端电量 all in one card) + "智能建议" card — exactly the layout style the latest rules reject | Prior photo | — | None | **FAIL (style non-compliant)** |
| E4 | Voice screen | Central energy wave, 正在聆听 | **Does not exist** | None | — | None | **NOT IMPLEMENTED** |
| E5 | Voice Result screen | Hermes answer | **Does not exist** | None | — | None | **NOT IMPLEMENTED** |
| E6 | Explain screen | 为什么现在？ | Screen enum value exists (`GH_SCREEN_EXPLAIN`) but renderer ignores `model->screen` entirely; no explain content model | None | — | None | **NOT IMPLEMENTED** |
| E7 | Proposal screen | reason, devices, current→target, impact, expiry | Dashboard proposal card: reason + at most 2 of 4 actions, format "capability -> target". No current value, no expected impact (`expected_impact`/`do_nothing` fields declared, never parsed), no expiry/freshness (`expired` never set) | — | — | Incomplete data display | **PARTIAL** |
| E8 | Approval confirmation screen | 将影响 X 台设备 / 长按确认 | Long-press button exists inside proposal card; but see O2 — it can never activate | — | — | None | **PARTIAL** |
| E9 | Execution Timeline screen | WAITING/RUNNING/VERIFYING/… from backend | **Does not exist.** After approval the model sets `GH_SCREEN_EXECUTION` but the UI never renders any execution view; state reverts via next 5 s snapshot poll | None | — | None | **NOT IMPLEMENTED** |
| E10 | Verification Result screen | 已验证 / 部分完成 / 已阻断 with before/after | **Does not exist** | None | — | None | **NOT IMPLEMENTED** |
| E11 | Offline screen | 无法连接光衡 + 上次同步 | No dedicated screen; offline is a text label in the status line. No last-sync value | None | — | None | **FAIL** |
| E12 | Device Status screen | 终端电量 / Wi-Fi / HTTPS / Cloud Sync | **Does not exist** | None | — | None | **NOT IMPLEMENTED** |
| E13 | Pairing page style | Watch-like dedicated page | Good: dark gradient, halo, 6-digit cells, minimal text | Prior photo | Real | None | **PASS** |
| E14 | No engineering strings in production UI | 正在同步时间… not "TIME SYNCING" | Strings are Chinese product copy ("同步时间"), but the combined status line "正在连接 \| 持续观察" is still an engineering-style compound; no "上次同步 10:32" pattern | — | — | None | **PARTIAL** |

### F–M. Voice chain

| # | Feature | Expected | Current Implementation | Runtime Evidence | Hardware Evidence | Fake Data Risk | Verdict |
|---|---|---|---|---|---|---|---|
| F1 | Voice button on Ambient | VOICE_IDLE → VOICE_LISTENING; mic open only in session | **No voice entry exists anywhere in the UI**; `voice_active` field never set true | None | — | None | **NOT IMPLEMENTED** |
| F2 | Runtime mic session + Audio Ring Buffer | Continuous PCM capture during session | Mic read exists only inside one-shot boot HIL; no ring buffer, no session lifecycle | None | Boot HIL proves hardware capable | None | **NOT IMPLEMENTED** |
| F3 | Voice wave from real PCM RMS/peak/VAD | HIL: quiet→low, speak→rise, stop→fall | **No wave UI at all.** Nothing to fake — also nothing real | None | — | n/a | **NOT IMPLEMENTED** |
| F4 | Audio upload to GuangHeng Speech Gateway | Mic→ES8311→I2S→ring buffer→HTTPS/WSS→Gateway | **No upload code.** No speech endpoint known in firmware; backend speech-gateway availability unverified (backend source not on this workstation) | None | — | None | **NOT IMPLEMENTED (endpoint verification BLOCKED EXTERNAL)** |
| F5 | ASR | Real speech-to-text | **No ASR path.** No fixed transcript exists either (at least nothing is faked) | None | — | None | **NOT IMPLEMENTED → VOICE ASR = FAIL** |
| F6 | Hermes intent chain | READ_STATUS/EXPLAIN/WHAT_IF/…/APPROVAL_INTENT/AMBIGUOUS | **Not wired on device.** Hermes runs on the PC (`D:\Hermes`), but the ESP32-S3 has no transcript/intent path | None | — | None | **NOT IMPLEMENTED** |
| F7 | Voice tests 1–8 (读状态/解释/what-if/提案/目标/批准意图/澄清) | Real end-to-end | **Not executable — prerequisites F1–F6 missing** | None | — | None | **NOT RUN** |

### N–Q. Proposal / Approval / Execution / Verification

| # | Feature | Expected | Current Implementation | Runtime Evidence | Hardware Evidence | Fake Data Risk | Verdict |
|---|---|---|---|---|---|---|---|
| O1 | Proposal from backend truth | Device displays backend action set | `pending_action_set` parsed (id, reason, items: device_name/capability/target_value); approve POST includes `nonce` + `action_set_version` (single-use, version-bound) | Prior HIL: challenge fields present | — | None | **PARTIAL (fields incomplete, see E7)** |
| O2 | Long-press approval activation | Real touch long-press 1.2–1.5 s; enabled only when backend online + pending | Mechanism is real (LVGL press/release + 50 ms timer, 1400 ms, disabled state, offline guard, host-tested). **BUT `gh_model_can_approve()` also requires `proposal.proposal_id[0] != '\0'`, and `gh_backend_refresh()` NEVER parses proposal_id from the backend** — so in production runtime the button is permanently disabled. Host tests pass only because the test manually writes a proposal_id | None on device | — | None | **FAIL — CRITICAL dead path: approval can never fire on real hardware** |
| O3 | Approval request → Backend truth | Device sends request; backend is Approval Truth | `POST /api/v1/companion/action-sets/{id}/approve` with nonce+version, authenticated headers | Cannot fire (O2); endpoint existence implied by backend design, not re-tested this session | — | None | **PARTIAL (blocked by O2)** |
| O4 | Action Set reuse (backend 1.5.0 multi-device pipeline) | No second execution path on device | Firmware has no local execution engine; it only approves and re-polls snapshot — correct boundary | Source inspection | — | None | **PASS (boundary)** |
| O5 | Execution Timeline from backend | WAITING/RUNNING/VERIFYING/VERIFIED/FAILED/BLOCKED/SKIPPED states | **Not implemented.** No timeline UI; no per-action status rendering; no UI timer fake either (nothing is displayed at all) | None | — | None | **NOT IMPLEMENTED** |
| O6 | Smart Meter Verification UI (L1/L2/L3) | 已验证 only after Household Outcome Verification; before/after values; partial/blocked states | **Not implemented.** No L1/L2/L3 distinction anywhere | None | — | None | **NOT IMPLEMENTED** |

### R–V. Offline, secrets, fake data, sync

| # | Feature | Expected | Current Implementation | Runtime Evidence | Hardware Evidence | Fake Data Risk | Verdict |
|---|---|---|---|---|---|---|---|
| R1 | Offline safety: no approval/write while offline | Approval & writes blocked offline | `can_approve` requires `backend_online`; refresh failure clears it; button disabled; **no cached-approval replay path exists** (good) | Host unit tests cover offline-never-approves | — | None | **PASS (logic)** |
| R2 | Offline UX | 连接已中断 + 上次同步 | Label only; no last-sync; stale values not marked STALE | None | — | Frozen values as-if-current | **PARTIAL** |
| S1 | Secret boundary (firmware) | No HA token / DeepSeek key / Hermes key / SOLIX / SSH / TLS private key | Full source scan: none present. Only Companion device credential (NVS, never printed) + compile-time Wi-Fi SSID/PSK fallback in `sdkconfig` (`607` / `1535…`, HIL-only, must not ship in production build) | Source scan this audit | — | Wi-Fi PSK in sdkconfig (HIL fallback) | **PARTIAL** |
| S2 | Fake-data keyword audit | No fixture/mock/demo/sample/fake/hardcode/placeholder in production path | Scan of `main/` + `tests/`: no fixtures, no mocks, no GH_FIXTURE_MODE. Only hits are audio-HIL constants (880 Hz tone, gains) — TEST ONLY. No occurrences of 4.2/1.9/0.2/77/86/4200/1900/2300 as energy values | Scan this audit | — | Two fake-data issues exist despite keyword cleanliness: A7 (battery 0%) and D3 (source REAL hardcode) | **PARTIAL** |
| T1 | Flutter ↔ device shared truth | Same proposal_id visible in Flutter | Both consume the same backend; Flutter reads action sets. Device never displays proposal_id, so cross-check of a shared voice proposal cannot be performed yet | None | — | None | **PARTIAL** |
| T2 | MCP boundary = 14 tools, no new approve/execute/HA-write | Voice dev must not add tools | Firmware does not touch MCP at all (correct boundary). Local MCP (127.0.0.1:8001) not running during audit; 14-tool count is from prior record only | Prior record | — | None | **PARTIAL (not re-verified this session)** |
| T3 | Backend pytest | All backend tests pass | Backend source not on this workstation; prior record: 140 passed | Prior record | — | None | **BLOCKED EXTERNAL** |
| T4 | Flutter analyze/test | Clean + tests pass | Prior record: analyze clean, 15 tests pass; 3 test files exist incl. `backend_notification_sync_test.dart` | Prior record (not re-run this audit) | — | None | **PARTIAL** |
| T5 | Firmware host unit tests | Pass | `gh_model_tests.exe` executed during this audit: 5/5 passed (offline-never-approves, approval binds proposal, lift/magnet never approve) | Executed today | — | None | **PASS** |
| T6 | Serial logs / build output / flash hash evidence | HIL evidence preserved | Prior doc records flash hash `BC56ED45…`; fresh binary today 21:14; no serial log files stored in project (logs were transient monitor output) | Filesystem | — | None | **PARTIAL (no persisted serial logs)** |

---

## CRITICAL FINDINGS (ranked)

1. **Approval is dead on real hardware (O2).** `gh_backend_refresh()` never parses `proposal_id` from the backend snapshot, while `gh_model_can_approve()` requires it. The 长按确认 button is therefore permanently disabled in production runtime. The host tests mask this because the test fixture writes proposal_id by hand. This silently breaks the entire SEE→PROPOSAL→CONFIRM→EXECUTE→VERIFY closure.
2. **source_mode is hardcoded REAL (D3).** Backend reports simulator; Flutter shows 模拟器; the device displays "实时数据". Direct violation of the truthfulness rules (§35: Simulator ≠ 真实家庭).
3. **Companion battery displays a fabricated 0% (A7).** AXP2101 is only probe-checked at boot; `companion_battery_pct` is never assigned; UI shows "终端电量 0%". Spec requires "--" or real voltage when % is not computable.
4. **The whole voice chain is absent (F1–F7).** No voice button, no mic session, no ring buffer, no wave, no upload, no ASR, no Hermes intent. Voice P0 = FAIL until built. No fake wave/transcript exists either — the codebase has at least not faked any of it.
5. **UI is the rejected dense-dashboard style and the screen state machine is unused (E1–E14).** `model->screen` is never consulted by `gh_ui_render()`; Boot/Connect/Time/Backend/Voice/Voice Result/Explain/Execution/Verification/Device Status screens do not exist.
6. **Lift-to-Explain has no runtime IMU service (A5).** QMI8658 is read exactly once at boot; `gh_model_set_lifted()` is dead code.
7. **No last-sync / stale handling (B6, R2).** Offline freezes old values with only a text label.
8. **WSS not implemented (B7).** Current architecture is REST polling every 5 s. Either implement WSS or record an explicit architecture-change decision; do not pretend polling satisfies the WSS design.
9. **No execution timeline or smart-meter verification UI (O5, O6).** L1/L2/L3 distinction absent.
10. **MCP tool count and backend tests not re-verifiable from this workstation this session (T2, T3).**

## Fake-data classification summary

| Item | Classification |
|---|---|
| 880 Hz tone, speaker volume, mic gain constants (`gh_audio.c`) | TEST ONLY (boot HIL) |
| Pairing copy "安全连接 / 等待手机确认" | UI CONSTANT |
| `companion_battery_pct` displayed as 0% | **FAKE PRODUCTION DATA** |
| `source_mode = GH_SOURCE_REAL` hardcode | **FAKE PRODUCTION DATA (truthfulness)** |
| Energy numbers (PV/load/grid/SOC) | REAL BACKEND DATA (simulator server — allowed, but must be labeled) |
| QMI/RTC/PMU boot readings | REAL SENSOR DATA |
| No fixture/mock/demo/hardcode keyword hits | CLEAN |

## Deferred to P2 (do not score in P0)

- TMAG5273 driver — DEFERRED TO P2 (stub only, no fake magnetic state, `magnet_attached` never displayed)
- Magnet Calibration — DEFERRED TO P2
- Magnetic Attached/Detached, Advanced Magnetic Context, Phone/Dock Differentiation — DEFERRED TO P2

No UI currently shows 磁吸/桌面模式/便携模式 or "Magnetic unavailable" states — nothing to remove for P0.

---

## P0 REMAINING IMPLEMENTATION PLAN (proposed, STEP 2 — not yet executed)

Ordered by dependency and risk. Each group ends with Build → host tests → flash HIL gate before the next group starts.

**G1 — Truthfulness fixes (small, immediate P0 blockers)**
1. Parse real `source_mode` from backend snapshot (mirror Flutter's `'simulator'` field); map to 模拟环境/实时数据/等待数据; never hardcode REAL.
2. Companion battery: read AXP2101 battery %/voltage at runtime (or display "--" if percentage cannot be computed reliably); remove the constant 0%.
3. Parse and display `last_sync` (上次同步 HH:MM); mark data STALE after refresh failure instead of silently freezing values.

**G2 — Unbreak approval (CRITICAL)**
4. Parse `proposal_id` from snapshot; populate `model->proposal.proposal_id`; verify 长按确认 enables with a real pending backend action set.
5. Add host unit test asserting that a refresh-shaped snapshot (without manual fixture writes) yields an approvable model.

**G3 — UI restructure to watch-style screens**
6. Render by `model->screen` (currently ignored): Boot / Connect / Time / Backend / Ambient / Explain / Proposal / Approval / Execution / Verification / Offline / Device Status.
7. Ambient page: single central metric + bottom mini-cards (PV/Load/Grid/SOC), dark gradient, rounded geometry; remove the dense 4-line text block.
8. Offline page: 无法连接光衡 + 上次同步 HH:MM; product-state strings only.

**G4 — Runtime IMU + Lift-to-Explain**
9. Continuous QMI8658 sampling task (low-rate); real lift detection (orientation/accelerometer delta) → `gh_model_set_lifted(true)` → Explain screen. Lift never approves (already guaranteed by model tests).

**G5 — Voice chain (largest block; verify backend speech endpoint first — currently BLOCKED EXTERNAL)**
10. Voice button on Ambient → VOICE_IDLE → VOICE_LISTENING; mic open only inside session; mic off otherwise.
11. Runtime audio task + PCM ring buffer (16 kHz/16-bit/mono already proven by HIL).
12. Voice wave driven by real PCM RMS/peak (quiet→low, speak→rise, stop→fall). No timer/random animation as the voice amplitude.
13. HTTPS/WSS upload of PCM to GuangHeng Speech Gateway → real ASR → Hermes intent chain (READ_STATUS/EXPLAIN/WHAT_IF/PROPOSALS/GOAL/APPROVAL_INTENT/AMBIGUOUS).
14. Voice Result, Explain, and voice-generated Proposal screens; clarification flow for ambiguous device references.
15. Run voice tests 1–8 and record PCM/upload measurements (RMS quiet/speaking, peak, underrun/overflow, upload latency).

**G6 — Execution & verification rendering**
16. Per-action timeline states from backend snapshot (WAITING/RUNNING/VERIFYING/VERIFIED/FAILED/BLOCKED/SKIPPED); no UI timers.
17. Verification screen with L1/L2/L3 distinction, before/after values, 部分完成 states.

**G7 — Transport decision + evidence**
18. Decide WSS vs REST polling; if REST stays, record the architecture change explicitly in docs.
19. Persist serial/HIL logs, build output, flash hash for the final acceptance report; re-verify MCP tool count while gateway is running; re-run backend pytest (needs backend source access) and Flutter analyze/test.

**Constraints carried into STEP 2:** no fake TMAG/magnet/fixture data; no voice auto-approve; simulator data allowed but always labeled; no secrets in firmware; every group needs Build+Test+HIL evidence before PASS.
