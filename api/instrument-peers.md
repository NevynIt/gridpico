# Optional MIDI instrument peers

**Owner** Architect. **Status** Approved boundaries. [Browser MIDI](browser-midi.md) · [Local production router](local-router.md).

A sound-generating peer is an optional, independently launched MIDI destination. The router delivers MIDI through existing selectable ports and owns route and note-release accounting. A peer owns its synthesis engine, voices, settings and local audio output. The Gridpico kernel and Pico firmware handle MIDI. Independently running components continue to exchange MIDI or the independent serial CLI byte stream. An audio file retrieved for testing is a bench artifact.

## Stock FluidSynth proof on `rpi0`

Use stock FluidSynth with a documented SoundFont and a selected ALSA sequencer MIDI input. Route a short held Note On/Off pair through the production Pi router to that port. Capture a short WAV from the live file driver or another verified capture sink, record the MIDI receipt and verify the tone, release tail and return to the background level. Retrieve the WAV and evidence manifest by SFTP and verify the checksum. Record package version, SoundFont provenance, rights and checksum, selected route and audio settings. Keep an offline MIDI-file render, actual routed audio and physical listening as separate results. Label an RTP-routed WAV only after the actual cross-host leg is exercised. [FluidSynth live audio driver](https://www.fluidsynth.org/api/settings_audio.html) · [MIDI-file rendering](https://www.fluidsynth.org/api/group__file__renderer.html).

## Standalone FluidSynth wrapper

After the stock proof, the wrapper can launch and stop the installed engine and select the local MIDI input, SoundFont and audio sink or file. Give it its own configuration and lifecycle. Reuse the system FluidSynth package for the first wrapper. Review the exact licensing and notices before distributing any library, SoundFont or rendered sample with the product. The production router sees the wrapper's MIDI port; its release, route and CLI contracts apply normally. [FluidSynth licensing](https://www.fluidsynth.org/wiki/LicensingFAQ/).

## Standalone browser FM instrument

Open one selected receive binding from the shared browser MIDI layer. A user enables Web Audio explicitly. Start with one built-in FM voice and ordinary Note On and Note Off. Interpret velocity-zero Note On as Note Off. Keep a bounded local voice allocation and release the matching voice on Note Off. Immediately silence or cancel remaining voices on disconnect, input change or audio disable. The FM page owns its DSP and local settings independently of Monitor.

The first acceptance case uses an actual routed Note On/Off, observes a sounding local FM voice and its release, and checks silence after disconnect. Package the HTML, engine and built-in voice for offline use. Verify direct-file playback and selected port permissions on the nominated browser. A local offline server can serve the same assets when the browser requires a served origin. Add the landing-page link when the instrument ships. Pin the engine version and audit code, dependency, patch and sample rights before bundling. The initial profile needs the approved Note On/Off path only. [Web Audio](https://www.w3.org/TR/webaudio/) · [Browser MIDI path](browser-midi.md#shared-in-page-adapter).
