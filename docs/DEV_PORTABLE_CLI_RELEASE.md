# Maintained DEV portable CLI release

The portable CLI overlay completed its authorized cutover to the maintained DEV localhost service at `127.0.0.1:8767`. The acceptance below records the exact running release and the browser workflow at cutover.

## Verified maintained release

| Pin | Identity |
| --- | --- |
| Maintained source | [`cd6f6e2b0b90e2e297e3ffc0f062cbcf1378b9a6`](https://github.com/NevynIt/gridpico/commit/cd6f6e2b0b90e2e297e3ffc0f062cbcf1378b9a6) plus approved six-path overlay SHA-256 `14f2ae12d9ce315d2e8f8cca470f017fb49b297563f277a63d925dcbefe3cab2` |
| Cutover receipt (`READY.md`) SHA-256 | `c0f977b6991c2559619077e67c5114731f90a2edc0ca2c8a2beb606d9c25b6bd` |
| Cutover evidence manifest SHA-256 | `55d19087c47c44e023cd2e035c0f53c9d3a34c43242a47dd4e7d9ae7afe674aa` |
| Actual Runner SHA-256 | `1ba597dbff08d5a7e8ee890c3b4a654f0cc3a10563627e99fddbb792233a7931` |
| Served bundle manifest SHA-256 | `bcd68f1578a624927af31e49cc6857c72ae98e7366d6ffc30328bed7fb9c9396` |
| Served bundle ZIP SHA-256 | `d4bfa78359265e1368cb8a04f50b04ab0dda342d1cc92bd35dfe134e1a667cb1` |

All served assets, the bundle manifest and ZIP matched sealed staging bytes. The final validator run used the existing ownership guard and completed acceptance before committing the cutover. The startup helper negotiated Machine with the exact `FORMAT MACHINE\r\nOK\r\n` acknowledgement before typed Routing reads and a guarded `1 → 42` change. Stock Chrome separately verified Human help in Diagnostics and Machine-negotiated Routing help and pages. The attached browser role read the committed route as Active.

Stock Chrome opened static FM 42. FM 42, Monitor 5 and MIDI Log 6 each received `903060` followed by `803000`. With browser audio enabled, FM RMS rose to 0.00285025 and returned to zero. Monitor RMS rose to 0.00137151 and returned to zero. The existing relay listener was preserved and the acceptance clients closed.

## First live attempt and verified rollback

The first live attempt used the same six-path overlay. The ownership validator rejected a relative profile argument through literal path comparison. The guardian stopped the candidate and restarted the exact `cd6f6e2` baseline. Recovery resolved the effective paths, verified the baseline Runner `ce088da8c2ef6dc8df89bbb9ac162d110517f9a32ddd1c6823e9b8f645466cba` and served manifest `24f9f7bbf16e0ccec5e86b3c803f5315650c2cb82ef4bf39c0e593a49b411a16`, restored the guarded static FM route, and rechecked Chrome FM, Monitor and Log. The rollback receipt has SHA-256 `758c58902fee8fa721a4115764b594276abc10428afe750824869a5e964aae8f`. The corrected retry reached the committed release above.

## Full-main rehearsal hold

The isolated [`7193d43` rehearsal](https://github.com/NevynIt/gridpico/commit/7193d43a785a889f5ef713e4f281de8dab6f5d11) seeded FM 42 successfully, but Web FM auto-joined port 7. That route stayed Off and produced no FM audio. The isolated rehearsal restored FM 42 using the retained baseline. The full-main candidate remains held for maintained DEV. The rehearsal receipt has SHA-256 `3aaf71b8359e7652e1fb32e881306e8df6b6a922ae2ef7e773680da971fee5b5`.

The maintained DEV receipt covers the localhost browser workflow. Pi, Pico, physical audio and external M5 playback remain separate acceptance scopes.
