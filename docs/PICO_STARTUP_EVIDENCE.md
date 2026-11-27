# DEV Pico startup and rollback receipts

These two rollout receipts identify exact images and record startup plus verified recovery material. Both trials completed without needing an actual rollback. Actual rollback testing is a separate stop/diagnostic acceptance item and is not evidenced here. This page does not include firmware images or raw captures. See the [Pico target and recovery contract](../api/pico-target.md#recovery-and-release) for the maintained procedure.

## DEV Pico Matrix-24 8bc8c620 USB generation 11

**Image identity.** UF2 SHA-256 `8bc8c6208bfab9d71561cdff6794836341ec035f45cb9424bfa9793ed4dfe2a2`; ELF SHA-256 `b74493c4dadc359a052a5c82823429b8291da65ddf883ee16bb86588aacc0a9a`. The installed full-flash image matched SHA-256 `93a1d0bc90a6ccc91d1a0a330ae02ef9fee4346cec2f8a510ddece2e5917e774`.

**Verified.** On the identified 2 MiB DEV board, the original full flash (`034a6741209b74a64b78ad769bfc1673c30fa44d7e6ab290786f17cc330a7868`) was read and verified before install. The complete 114,688-byte rollback prefix (`2452fee7380a5ef140a035fc4e5a27a7ba114a28bfde132701375aa8e3eb0162`) and untouched tail at `0x1001c000` (`0aa7ef7488679521d6668d71332cd345d0c2eecd425b15b4bbe75220e18c6249`) were verified. After one approved prefix install, the candidate image and unchanged tail were verified. Normal USB generation 11 started with CDC and both native MIDI cable directions. Four USB MIDI devices mounted. Routing reported 20 Active and zero Pending managed routes, with 16 Active cells placed. The rollout ended normally; no rollback was needed.

**Limits of this receipt.** The 24-slot linked capacity was not fully exercised. Computer MIDI Out was unavailable and unplaced. Passive observation did not capture an attributable fresh Note On/Off pair, and the trial did not verify physical APC LED feedback. These are rollout-scope limits, not claims about later trials.

## DEV Pico7 7f52985f USB generation 14

**Image identity.** UF2 SHA-256 `7f52985f1d14c81d0266f871b0a14e08a902ac77f34dcbe29368c94f7fea5b13`; ELF SHA-256 `930a0fadf49ef99c0929f6e434341994641d7ff51db03d7d5d85fb33aa80432c`. The installed whole image matched SHA-256 `7ab7821349965effec0d8167ad46b3f71228cb82f18192b4c0d0db96361e94a6`.

**Verified.** The same board was identified in ROM and its Matrix-24 baseline (`93a1d0bc90a6ccc91d1a0a330ae02ef9fee4346cec2f8a510ddece2e5917e774`) verified before install. The complete 114,688-byte rollback prefix (`dd8f83296b0625f3aa0675f0ad84c09de83069d09ff508216dedeff5c64eb148`) and tail at `0x1001c000` (`0aa7ef7488679521d6668d71332cd345d0c2eecd425b15b4bbe75220e18c6249`) were verified before and after. One authorized prefix install produced the exact candidate image. Normal USB generation 14 started with native descriptors, CDC and both MIDI cable directions. Four devices mounted. Routing reported 20 Active and zero Pending managed routes, and PIO advanced without a retained transfer fault. The rollout ended normally; no rollback was needed.

**Limits of this receipt.** The seven-device profile and all 24 linked branches were not saturated. Computer MIDI Out was unavailable in the rollout smoke. Passive observation saw timing clock and active sensing but no attributable fresh Note On/Off pair. Musical playback and physical LED behavior were outside this startup/rollback receipt.

The hashes identify the checked artifacts and readbacks. These receipts did not record a source commit, so this page does not infer one.
