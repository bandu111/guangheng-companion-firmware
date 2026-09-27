# GuangHeng Energy Companion P0 Final Acceptance

Hardware target: Waveshare ESP32-S3-Touch-AMOLED-1.8  
MCU: ESP32-S3  
Firmware: ESP-IDF 5.5.5  
Backend: GuangHeng 1.6.0 (`https://43.155.204.194`)

This document preserves the original failures and records each correction as
`Before -> Fix -> After`. A later gate must not be marked PASS from host tests
alone when its acceptance criteria require ESP32-S3 Hardware-in-the-loop.

## G1 — Truthfulness Fix

### Implementation

#### Source mode

- Before: firmware assigned `GH_SOURCE_REAL` without reading the Backend
  payload, so Simulator data could be presented as real data.
- Fix: parse `energy.source.source_mode` from the authenticated production
  `/api/v1/companion/snapshot` response. Supported values are `real`,
  `simulator`, `replay`, and explicit `unknown`.
- After: the current production Simulator is rendered as `模拟环境`; no
  hostname- or environment-based inference remains.

#### ESP32-S3 companion battery

- Before: the zero-initialized percentage was rendered as `终端电量 0%` even
  when no reliable battery SOC was available.
- Fix: add a read-only AXP2101 runtime power service for battery presence,
  hardware e-gauge percentage, voltage, and charging state. No voltage-to-SOC
  estimation is used.
- After: a valid AXP2101 percentage is displayed when available; otherwise the
  UI displays `终端电量 --` and only appends voltage when a valid voltage is
  available.

#### Last successful synchronization and stale state

- Before: an old Snapshot could remain visible without clearly becoming stale.
- Fix: record `last_successful_sync_at` only after a successful Snapshot;
  network, TLS, HTTP, authentication, and payload failures preserve the last
  values but set `stale=true`.
- After: the UI reports `连接已中断`, the last synchronization time, and
  `已过期`. A later successful Snapshot clears stale state.

### Test

- Host build: PASS.
- Host tests: PASS, 1/1 test executable. Coverage includes a real-shaped nested
  Simulator Snapshot, replay/unknown mapping, and preservation of
  `last_successful_sync_at` after an offline transition.
- ESP32-S3 firmware build: PASS.
- Firmware image: `guangheng_energy_companion.bin`, 3,200,128 bytes.
- Firmware SHA-256:
  `32A29F9147AB6966BFC7B27813912F6FECBA5FB88A57FFD022A1B71C46FE81A3`.
- Flash verification: PASS; all written regions passed hash verification.

### ESP32-S3 Hardware-in-the-loop Evidence

- ESP32-S3 QFN56 revision 0.2 detected through USB Serial/JTAG on COM10.
- 16 MB Flash and 8 MB PSRAM detected; PSRAM memory test passed.
- AXP2101 runtime read: PASS. The connected hardware reported
  `present=false`, with percentage and voltage unavailable; the UI therefore
  displays `终端电量 --`, not a fabricated `0%`.
- Production TLS certificate validation: PASS.
- Authenticated production Snapshot: HTTP 200.
- Runtime log reports `source_mode=SIMULATOR stale=false`; AMOLED confirmation
  reports `模拟环境`.
- Interruption evidence records
  `stale=true last_successful_sync_at=1790344687`, proving a previous successful
  synchronization was retained rather than reset.
- Wi-Fi recovery and the next authenticated Snapshot returned HTTP 200 and
  restored `source_mode=SIMULATOR stale=false`.
- User physical-screen confirmation for the interrupted/stale presentation:
  PASS on 2026-09-25.
- Serial evidence:
  `docs/hil_logs/G1_2026-09-25_final_serial.log`.

### Verdict

**G1 PASS — 2026-09-25.**

G2 and later gates are not covered by this verdict.

## G2 — Real Action Set Approval

### Before

- The firmware displayed proposal-like information without parsing the complete
  production Action Set approval contract.
- Approval truth could not be proven from a real four-item Action Set, and
  repeated HTTPS client allocation caused intermittent TLS allocation failure.

### Implementation

- Parsed the real `pending_action_set` and `approval_challenge` structures,
  including Action Set id, child proposal ids, version, expiry, device actions,
  expected impact, and single-use nonce without logging the nonce.
- `can_approve` now requires a fresh online Snapshot, PENDING non-expired Action
  Set, valid id/version/nonce, and a complete pending proposal.
- Added physical-screen 1400 ms hold detection; early release cancels and a
  completed hold emits only one approval callback.
- Reused the configured HTTPS client and moved large TLS allocations to the
  ESP32-S3 board's verified 8 MB PSRAM. Certificate verification remains enabled
  and the CO5300 AMOLED retains internal DMA memory.

### Host Test

- PASS: 1/1 executable, 12 test scenarios.
- Coverage includes the production four-item Action Set #3 shape, the complete
  negative approval matrix, expiry/non-PENDING rejection, offline rejection,
  and proof that lift/magnet signals cannot approve.

### ESP32-S3 Hardware-in-the-loop

- Five consecutive authenticated production Snapshot cycles remained successful
  after the TLS/PSRAM correction, without TLS allocation or AMOLED DMA failures.
- Device parsed Action Set #3 as PENDING with four actions and first proposal id
  36.
- Physical touch log recorded hold start followed by the threshold at exactly
  1400 ms.
- Approval POST returned HTTP 200 exactly once.
- Backend result: Action Set #3 `SUCCEEDED`, verification `VERIFIED`, measured
  grid change 2300 W; child proposals 36–39 all created one execution each
  (34–37), all `SUCCEEDED` with `READBACK_VERIFIED`.
- Serial evidence: `docs/hil_logs/G2_2026-09-26_attempt5_serial.log`.
- Backend evidence summary: `docs/evidence/g2/action_set_3_verified.md`.

### Verdict

**G2 PASS — 2026-09-26.**

This verdict covers the real ESP32-S3 long-press approval and server-side
execution/readback result only. It does not claim G3 or later gates.

## G3 — UI State Machine Rebuild

### Before / Root Cause

- The original firmware rendered a dense dashboard and did not use
  `model->screen` as an actual page router.
- Connection, Explain, Offline, and Device Status were not independent product
  screens. Runtime refreshes could not preserve a user's current page.
- The first gesture implementation depended on LVGL `GESTURE`; the CST816S
  integration did not emit it reliably through nested cards. A Unicode ellipsis
  was also outside the embedded CJK font range and appeared as a box.

### Implementation

- Added persistent 368 x 448 pages for Boot, Wi-Fi, Time Sync, Backend,
  Pairing, Ambient, Explain, Proposal, Approval, Execution, Verification,
  Offline, and Device Status. Pages are created once and updated in place.
- Added central runtime routing with explicit priority for pairing, connection,
  offline, pending proposal, and user-active pages.
- Replaced the dense dashboard with a watch-style Ambient hierarchy: one
  primary energy state, three compact secondary values, a truthful
  `模拟环境` source badge, and stale treatment.
- Explain uses only deterministic relations from the current Backend Snapshot;
  no LLM text or hardcoded runtime energy values were added.
- Added a 220 ms page transition. Touch navigation now measures real press and
  release coordinates and uses page context, so it remains usable with either
  physical connector orientation.
- Replaced unsupported Unicode ellipses with font-safe product copy.

### Host Test

- PASS: router lifecycle, pending Proposal to Approval, offline recovery, and
  preservation of Device Status are covered.
- Release tests explicitly undefine `NDEBUG`, so assertions execute rather than
  being compiled out.

### ESP32-S3 Hardware-in-the-loop

- Real routes observed on COM10: Boot -> Wi-Fi -> Time Sync -> Backend ->
  Ambient.
- Production HTTPS Snapshot remained valid and rendered Simulator truthfully.
- CST816S coordinate evidence captured an actual swipe; the final implementation
  uses the measured short-travel behavior instead of assuming desktop-sized
  gesture distances.
- User confirmed on the physical ESP32-S3 AMOLED that horizontal page switching
  and the corrected Chinese connection text work on 2026-09-26.
- Evidence:
  `docs/evidence/g3/G3_2026-09-26_swipe_coordinate_serial.log`,
  `docs/evidence/g3/G3_2026-09-26_swipe_final_serial.log`.

### Verdict

**G3 PASS — 2026-09-26.**

This verdict does not claim G4 Lift-to-Explain calibration.

## G4 — Runtime Lift-to-Explain

The continuous QMI8658 path and overlap-safe capability buffer are present and
the partial ESP32-S3 calibration log contains real lift events. The required
20-case physical calibration matrix was not completed before work moved to G5.

**G4 PARTIAL — do not report PASS.**

## G5 — Real Voice Chain

### Implementation

- The Ambient screen has a voice entry and dedicated Listening, Transcribing,
  and Voice Result pages in Chinese watch UI.
- ES8311/I2S captures real 16 kHz, 16-bit, mono PCM only during a voice
  session. A FreeRTOS ring buffer separates the producer and consumer.
- Wave bars are calculated from real RMS/peak samples. No timer, random
  animation, or fixed transcript is used.
- A bounded PCM WAV is assembled in verified ESP32-S3 PSRAM and sent through
  the existing certificate-verified authenticated HTTPS channel to
  `/api/v1/companion/voice`.
- Backend Faster Whisper performs real ASR, then the existing Official Hermes
  chain supplies the intent and final answer. Raw WAV is deleted after ASR;
  SQLite stores metadata only.
- `APPROVAL_INTENT`, `REJECT_INTENT`, and ambiguous references do not enter
  Hermes or mutate state. Voice approval only reveals the real pending Action
  Set and requires the existing physical long press.

### Automated evidence

- Backend targeted voice/Companion tests: 19 passed.
- Backend complete regression: 156 passed.
- ESP32-S3 host model/parser tests: PASS.
- ESP-IDF 5.5.5 ESP32-S3 build: PASS; binary `0x314050` bytes, 49% of the
  smallest app partition remains free.

### Hardware-in-the-loop

Real speech capture, RMS quiet/speaking response, upload latency, ASR text, and
Hermes tool trace require the newly built image plus production Backend 1.6.0.
They are not proven by host tests.

**G5 IMPLEMENTED / ESP32-S3 HIL PENDING.**

## G6 — Execution and L1/L2/L3 Verification UI

- Companion Snapshot 1.1 exposes the latest backend `current_action_set`.
- ESP32-S3 parses Action Set status, each child execution id/status/result,
  unified verification status/message, and available Smart Meter before/after
  values.
- Execution UI renders real per-action states. It does not advance from a UI
  timer.
- Verification UI distinguishes L1 command acceptance, L2 per-device readback,
  and L3 Smart Meter household outcome. Partial/blocked/failed states remain
  visible and are not promoted to verified.
- Host tests cover a verified backend-shaped Action Set plus PARTIAL /
  NOT_VERIFIED and Smart Meter UNAVAILABLE states.
- The completed Verification page is not modal. Its button and a horizontal
  swipe both return to Ambient and acknowledge only the displayed Action Set
  id, preventing later Snapshot refreshes from forcing the same historical
  result back on screen.

**G6 IMPLEMENTED / live post-execution ESP32-S3 screen HIL PENDING.**

## G9 — Smart Meter Verification UI

- The watch screen now treats verification as one focused task instead of a
  compact dashboard: final outcome, real before/after grid power, then L1/L2/L3
  evidence.
- `已验证` is shown only when the Action Set is `SUCCEEDED` and Backend unified
  verification is `VERIFIED`. A contradictory `PARTIAL + VERIFIED` payload is
  deliberately not promoted to green success.
- `PARTIAL`, child `BLOCKED`/`FAILED`/`SKIPPED`, `NOT_VERIFIED`, and
  `UNAVAILABLE` have distinct non-success states. When L2 succeeds but L3 does
  not, the UI explicitly says `设备已调整` and `家庭能源效果未验证`.
- Before/after values are displayed only when both values exist in the real
  Companion Snapshot. Missing data renders `--`; no zero or fixture value is
  substituted.
- The obsolete bottom button was removed. Horizontal swipe returns to the
  household-energy page, matching the current gesture language.
- ESP32-S3 host tests pass, the ESP-IDF 5.5.5 `esp32s3` build passes, and the
  image was flashed to the verified ESP32-S3 on COM10. Serial HIL confirms TLS
  certificate validation, Snapshot HTTP 200, `source_mode=SIMULATOR`,
  `stale=false`, and routing to `VERIFICATION` without reset or stack failure.

**G9 IMPLEMENTED / ESP32-S3 runtime route PASS / final visual inspection pending.**

## G7 — Transport, Regression, and Evidence

### Architecture decision

Voice uses bounded authenticated HTTPS REST batch WAV upload. This is an
explicit product decision, not a claim that WSS was implemented. Push-to-talk
audio is short, retryable, and keeps the microphone/network idle outside the
session. WSS remains a future latency optimization.

See Backend document `docs/companion_voice_architecture.md` for the frozen
audio contract, privacy lifecycle, safety boundary, and failure behavior.

### Regression

- Backend: 156 passed.
- Flutter: `flutter analyze` reports no issues; 15 tests passed.
- MCP registry: exactly 14 allowed tools; no approve, execute, Home Assistant
  write, or device-write tool was added.
- ESP32-S3 host tests: PASS.
- ESP32-S3 clean firmware build: PASS.

### Current verdict

Backend 1.6.0 and Official Hermes are deployed behind production HTTPS. The
voice endpoint, real Faster Whisper model, Hermes tool calls, and the existing
14-tool boundary have passed server-side E2E. Public probes confirm that only
443 is reachable among the checked application/internal ports; 8000, 8001,
8123, 502, 8642, 18555, 2375, and 2376 remain unreachable from the public
network.

The current ESP32-S3 image was built and flashed successfully. Binary size is
`0x314060` bytes, 49% of the smallest app partition remains free, and SHA-256
is `12071CDD4A8676D145A976664DAEB9FEA95415656A3B74427DF659455B682EEA`.
Real on-device speech capture/upload and the physical negative safety cases are
still required before G5-G7 can be marked fully accepted.

**G7 AUTOMATED PASS / PRODUCTION + ESP32-S3 HIL PENDING.**
