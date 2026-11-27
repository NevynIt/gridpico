# DEV Pico startup and rollback receipts

These two rollout receipts identify exact images and record startup plus verified recovery material. Both trials completed without needing an actual rollback. The separate [stop diagnostics](#stop-diagnostics-and-actual-rollback) below did require a rollback. This page does not include firmware images or raw captures. See the [Pico target and recovery contract](../api/pico-target.md#recovery-and-release) for the maintained procedure.

## DEV Pico Matrix-24 8bc8c620 USB generation 11

**Image identity.** UF2 SHA-256 `8bc8c6208bfab9d71561cdff6794836341ec035f45cb9424bfa9793ed4dfe2a2`; ELF SHA-256 `b74493c4dadc359a052a5c82823429b8291da65ddf883ee16bb86588aacc0a9a`. The installed full-flash image matched SHA-256 `93a1d0bc90a6ccc91d1a0a330ae02ef9fee4346cec2f8a510ddece2e5917e774`.

**Verified.** On the identified 2 MiB DEV board, the original full flash (`034a6741209b74a64b78ad769bfc1673c30fa44d7e6ab290786f17cc330a7868`) was read and verified before install. The complete 114,688-byte rollback prefix (`2452fee7380a5ef140a035fc4e5a27a7ba114a28bfde132701375aa8e3eb0162`) and untouched tail at `0x1001c000` (`0aa7ef7488679521d6668d71332cd345d0c2eecd425b15b4bbe75220e18c6249`) were verified. After one approved prefix install, the candidate image and unchanged tail were verified. Normal USB generation 11 started with CDC and both native MIDI cable directions. Four USB MIDI devices mounted. Routing reported 20 Active and zero Pending managed routes, with 16 Active cells placed. The rollout ended normally; no rollback was needed.

**Limits of this receipt.** The 24-slot linked capacity was not fully exercised. Computer MIDI Out was unavailable and unplaced. Passive observation did not capture an attributable fresh Note On/Off pair, and the trial did not verify physical APC LED feedback. These are rollout-scope limits, not claims about later trials.

## DEV Pico7 7f52985f USB generation 14

**Image identity.** UF2 SHA-256 `7f52985f1d14c81d0266f871b0a14e08a902ac77f34dcbe29368c94f7fea5b13`; ELF SHA-256 `930a0fadf49ef99c0929f6e434341994641d7ff51db03d7d5d85fb33aa80432c`. The installed whole image matched SHA-256 `7ab7821349965effec0d8167ad46b3f71228cb82f18192b4c0d0db96361e94a6`.

**Verified.** The same board was identified in ROM and its Matrix-24 baseline (`93a1d0bc90a6ccc91d1a0a330ae02ef9fee4346cec2f8a510ddece2e5917e774`) verified before install. The complete 114,688-byte rollback prefix (`dd8f83296b0625f3aa0675f0ad84c09de83069d09ff508216dedeff5c64eb148`) and tail at `0x1001c000` (`0aa7ef7488679521d6668d71332cd345d0c2eecd425b15b4bbe75220e18c6249`) were verified before and after. One authorized prefix install produced the exact candidate image. Normal USB generation 14 started with native descriptors, CDC and both MIDI cable directions. Four devices mounted. Routing reported 20 Active and zero Pending managed routes, and PIO advanced without a retained transfer fault. The rollout ended normally; no rollback was needed.

**Limits of this receipt.** The seven-device profile and all 24 linked branches were not saturated. Computer MIDI Out was unavailable in the rollout smoke. Passive observation saw timing clock and active sensing but no attributable fresh Note On/Off pair. Musical playback and physical LED behavior were outside this startup/rollback receipt.

The hashes identify the checked artifacts and readbacks. These receipts did not record a source commit, so this page does not infer one.

## Stop diagnostics and actual rollback

Both controlled stop trials initially returned `PENDING`. The second later reported `BUSY`. Neither produced a fresh Stop reboot cookie. In both trials the rollback owner restored the original prefix and physically verified the whole image.

### `e162c44b` · pending stop and rollback

Test UF2 SHA-256 `e162c44bf803b2f1abcc1a0a1467d7f6fab93ecd8973abfdf1479941bd133898`; ELF SHA-256 `9b9d9566361634b4d82ba526ed388f842f3588460a7c969ec0779280425ddcfd`. The candidate booted with native CDC and both MIDI cable directions. Stop request `A01B203C` returned `PAT2_STOP PENDING A01B203C`; USB generation did not change during the eight-second observation, and no fresh normal-reboot cookie appeared. After cooperative BOOTSEL, the rollback owner restored the complete 110,592-byte original prefix and physically verified the original 2 MiB image (`034a6741209b74a64b78ad769bfc1673c30fa44d7e6ab290786f17cc330a7868`) and tail (`ec9d9e0d8f11f8de5db4abf6de0671c5da6f8ddc8cddf1fc04d8fb825af449b4`). Original firmware `56c65dff` returned normally, with both holders clear.

### `692be0b7` · BUSY diagnosis and rollback

Test UF2 SHA-256 `692be0b707cce0c4a14de96aa8a1cbc765ee8936066b4938d84a13a40e9c6920`; ELF SHA-256 `37c20cc749aabafc85a8d37c9126b4af188476d58dbff3ba769302cc61d6afc4`. Stop request `A01B203C` returned `PENDING`. Same-generation read-only status then returned `PAT2_STOP BUSY A01B203C PROC1_PRECHECK 00000304`. The sampled FIFO status was `0x3` (VLD and RDY, without a sticky FIFO error). No FIFO data was read, so its value, source and meaning remain unknown. After cooperative BOOTSEL, the owner physically restored the complete 110,592-byte original prefix and verified the original 2 MiB image (`034a6741209b74a64b78ad769bfc1673c30fa44d7e6ab290786f17cc330a7868`) and tail (`ec9d9e0d8f11f8de5db4abf6de0671c5da6f8ddc8cddf1fc04d8fb825af449b4`). The owner then requested a no-write application reboot. A later passive, read-only boot check confirmed original firmware `56c65dff`, native CDC and both MIDI cable directions, with both holders clear; it used no writer or additional reset.

**Open finding.** The VLD word remains unread and its provenance is unknown. Neither stop trial observed a fresh normal-reboot cookie, so the BUSY sample does not establish a reset cause or prove the Stop-to-reboot path. The VLD contents/provenance and reset causality remain blockers. The post-rollback boot checks confirm the restored original image. The Matrix-24 and Pico7 rollout images have read-only PAT2 boot binding. Target Save, reset-to-empty and saved On/Off reboot acceptance remain open. No PAT2/settings/Save, route or Pi service changes were made, and no MIDI stimulus was sent.

## Independent Matrix-24 PAT2 host replay

The [historical fixture](../app/tests/pat2-matrix24-history/README.md) reproduces the independent strict C11 AddressSanitizer and UndefinedBehaviorSanitizer run. It checked 24-slot saturation, guarded Off-before-On admission, late attachment and fresh MIDI. The independent result transcript has SHA-256 `aea10e2aa35dcc3584c90d7bbd3f88e923a2296f54955d267192d0c753ef0fc6`. The fixture uses RAM storage on the host. Physical stop, flash write and reboot remain separate acceptance steps.
