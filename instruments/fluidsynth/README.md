# FluidSynth MIDI destination

Use Python 3, an installed Linux `fluidsynth` on `PATH`, and a local SoundFont you can use. The wrapper opens a named ALSA sequencer input. Select that input from your MIDI sender or router after starting it. The ALSA address can change between launches. Each run chooses one audio output.

Play through an ALSA PCM device:

```sh
./instruments/fluidsynth/run.py --soundfont /path/to/instrument.sf2 \
  --midi-name GridpicoFluid --alsa-device default
```

Record live MIDI as stereo 44.1 kHz, 16-bit PCM:

```sh
./instruments/fluidsynth/run.py --soundfont /path/to/instrument.sf2 \
  --midi-name GridpicoFluid --wav ./take.wav
```

Use a new WAV path in an existing directory. For parallel runs choose distinct MIDI names. The wrapper runs in the foreground and uses gain 0.65 with chorus and reverb disabled. Type `quit`, press Ctrl+C, or close stdin to stop the engine and finalize the recording. SIGTERM also requests a clean stop.

## Pi router recorder

The optional [Pi recorder](../../api/pi-recorder.md#optional-recording) uses the same stock FluidSynth file driver. Start launches a fresh, privately named engine. Once the router and ALSA bindings are ready, routed notes reach that engine. Stop releases the take's held notes and gives them one second to decay. The browser can download the latest WAV after the engine exits cleanly and the finalized file passes validation.

Build the portable runner and the private ALSA sender from the repository root on Linux with ALSA development headers installed.

```sh
cmake -S . -B build/pi-recorder -DGRIDPICO_ENABLE_ALSA=ON
cmake --build build/pi-recorder --target gridpico_local_router gridpico_recorder_midi
python3 -B -m unittest discover -v -s instruments/fluidsynth -p test_record.py
```

The host launches the private helper only for an armed take. Its inputs are the operator's SoundFont and ALSA sender paths, plus a fresh owner-only working directory created by the host. The helper writes `recording.wav` there. The host publishes it after successful completion and keeps the previous artifact if the take fails. The unit tests substitute test processes for FluidSynth and ALSA.
