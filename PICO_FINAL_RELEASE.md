# Pico7 final DEV firmware source

This branch preserves the exact firmware source for UF2 `a56bcafdc693820a2bdb9ed62c874f29897823b82100be9dec503b4f07294aa7`. The 260-file source snapshot at `05e3e543f4bb046be8111744e2a03c30a561d44e` matches the [qualified source manifest](release/pico-final/source-inputs.sha256). Main remains the portable development history. The source snapshot includes the earlier Pico interface and status light code used to produce this image.

The firmware uses normal Pico7, PIO USB, wide routing, read-only PAT2, the existing CDC CLI, physical APC and CDC CC123, and the read-only MSC asset drive. The exact image passed independent static audit and was installed on the DEV Pico on 2026-11-28 at 12:18:01 UTC. Physical flash readback, no-write BOOTSEL return, and the read-only FAT mount with all five file hashes passed. Real mounted-browser MIDI and FM audio and physical Shift+Record remain separate acceptance checks. The [artifact manifest](release/pico-final/artifacts.sha256) identifies the tested binary and the original recovered ELF. This source branch contains no firmware binaries.

## Reproduce the Pico build

Use the tool and SDK versions in [tool versions](release/pico-final/tool-versions.txt). The [external input manifest](release/pico-final/external-build-inputs.sha256) records their exact bytes and original local paths. Use matching inputs if your paths differ. The [configure recipe](release/pico-final/configure.sh) accepts CMake path overrides after the build directory. It builds only the Pico router profile.

From the root of this branch, generate the mounted pages from the pinned asset commit. Use a normal clone that contains the commit, fetching `main` if necessary. The staging script reads the asset checkout's Git revision to generate `VERSION.TXT`.

```bash
repo=$(pwd)
refs="$repo/release/pico-final"
sha256sum -c "$refs/source-inputs.sha256"

work=$(mktemp -d)
ln -s "$repo" "$work/source"
cp "$refs/configure.sh" "$work/"
git worktree add --detach "$work/asset-source" 4e7424265c8dd095d9e74d6de2b1392e09562361
(cd "$work/asset-source" && sha256sum -c "$refs/asset-inputs.sha256")
cmake -Dsource_dir="$work/asset-source" -Dasset_dir="$work/staged-assets" \
  -P "$work/asset-source/app/stage_gridpico_assets.cmake"
python3 app/pack_pico_usb_drive.py "$work/staged-assets" "$work/retained-tools.fat"
(cd "$work" && sha256sum -c "$refs/staged-assets.sha256")

"$work/configure.sh" "$work/build"
cmake --build "$work/build" --target gridpico_rp2040_router
```

Compare the BIN and UF2 SHA-256 values to [artifacts.sha256](release/pico-final/artifacts.sha256). ELF debug paths and map paths can vary with the local build root. Keep the original ELF untouched during section inspection.
