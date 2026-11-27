# Historical Matrix-24 PAT2 host fixture

Run from a checkout with historical commit ff518c4aaf594e5edb03b2274ea68a45afac29c7 in its local Git history:

```sh
bash app/tests/pat2-matrix24-history/replay.sh
```

The replay checks the original patches, exports that commit to a temporary directory, applies the two patches there, and checks the resulting source hashes. It runs the historical `bash app/tests/run_pat2_projection.sh --apply` before removing the temporary copy. The host runner requires Bash, Git, tar, SHA-256 tools, Python 3 and a C compiler with AddressSanitizer and UndefinedBehaviorSanitizer.

- [Backing prerequisite 44211435](pico-24-branch-backing.patch) selects 24 records under the original RP2040 Matrix flags. Its resulting header blob is `4e4611bc33f21911425b7079bca938a5a9ec4c21`.
- [Fixture delta ea01fe66](pat2-matrix24-fixture.patch) adds one disconnected Managed output to the fixture instance and seeds enough Pending branches to fill all 24 slots. Its resulting fixture blob is `c107018370aa05be93540bd129e2d30a6d8d4da7`.
- [Original patch checksums](SHA256SUMS) pin both historical inputs.

The run exercises the actual FULL outcome, guarded Off before ordered On, accepted no-op operator ownership, and late discovery. The boot-read donor is unnecessary for this replay. Both patches are stored as fixture data. The replay uses a disposable historical export and compiles only the host test.
