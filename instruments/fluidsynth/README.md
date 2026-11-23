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
