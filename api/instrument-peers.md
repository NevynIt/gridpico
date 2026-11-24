# Optional MIDI instrument peers

**Owner** Architect. **Status** Stock FluidSynth, the standalone wrapper and browser FM verified in their respective bench profiles. [Browser MIDI](browser-midi.md) · [Local production router](local-router.md).

A sound-generating peer is an optional, independently launched MIDI destination. The router delivers MIDI through existing selectable ports and owns route and note-release accounting. A peer owns its synthesis engine, voices, settings and local audio output. The Gridpico kernel and Pico firmware handle MIDI. Independently running components continue to exchange MIDI or the independent serial CLI byte stream. An audio file retrieved for testing is a bench artifact.

## Stock FluidSynth proof on `rpi0`

Use stock FluidSynth with a documented SoundFont and a selected ALSA sequencer MIDI input. Route a short held Note On/Off pair through the production Pi router to that port. Capture a short WAV from the live file driver or another verified capture sink, record the MIDI receipt and verify the tone, release tail and return to the background level. Retrieve the WAV and evidence manifest by SFTP and verify the checksum. Record package version, SoundFont provenance, rights and checksum, selected route and audio settings. Keep an offline MIDI-file render, actual routed audio and physical listening as separate results. Label an RTP-routed WAV only after the actual cross-host leg is exercised. [FluidSynth live audio driver](https://www.fluidsynth.org/api/settings_audio.html) · [MIDI-file rendering](https://www.fluidsynth.org/api/group__file__renderer.html).

The `699bac5` evidence records stock FluidSynth 2.4.4 with TimGM6mb 1.3-5 on the Pi. Independent checks verified the locally routed WAV's attack, release tail and final silence and matched its checksum after SFTP retrieval. Physical playback and an RTP-routed WAV remain open. [Pi audio evidence](../ROADMAP.md#approved-tonight-delivery)

## Standalone FluidSynth wrapper

The wrapper launches and stops the installed engine and selects the local MIDI input, SoundFont and audio sink or file. It owns its configuration and lifecycle and uses the system FluidSynth package. Review the exact licensing and notices before distributing any library, SoundFont or rendered sample with the product. The production router sees the wrapper's MIDI port and applies its normal release, route and CLI contracts. Independent Pi testing of `efc4707` verified routed Note On and Off, decay and final silence, and WAV finalization on Ctrl+C. [Run the wrapper](../instruments/fluidsynth/README.md) · [FluidSynth licensing](https://www.fluidsynth.org/wiki/LicensingFAQ/)

## Standalone browser FM instrument

Open one selected receive binding from the shared browser MIDI layer. A user enables Web Audio explicitly. Start with one built-in FM voice and ordinary Note On and Note Off. Interpret velocity-zero Note On as Note Off. Keep a bounded local voice allocation and release the matching voice on Note Off. Immediately silence or cancel remaining voices on disconnect, input change or audio disable. The FM page owns its DSP and local settings independently of Monitor.

The tool shipped in `130c9ed` at `/fm/` with an original built-in voice. The loopback home links to it and the staged offline home links to self-contained `FM.HTM`. Independent Chromium checks verified routed Note On and Off and voice cleanup through loopback WebSocket, plus playback and device-loss cleanup on a selected real ALSA Web MIDI port. Staged direct-file opening passed. Mounted Pico permissions and browser playback through an OS-exposed RTP port remain separate. Audit code, dependency, patch and sample rights before bundling future material. [Browser MIDI path](browser-midi.md#shared-in-page-adapter) · [FM evidence](../ROADMAP.md#approved-tonight-delivery)
