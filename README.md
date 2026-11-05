<div align="center">
  <h1>PitchGuide</h1>
  <p>A JUCE vocal pitch editor that is being extended to correct a new take toward a reference vocal and to show live pitch while you sing.</p>
</div>

## Origin, attribution, and license

PitchGuide is developed by Fearnus. It is a modified derivative of
[PitchNet](https://github.com/SessionLoops/PitchNet) by Session Loops, which is
itself a modified derivative of [HachiTune](https://github.com/KCKT0112/HachiTune)
by the HachiTune contributors. PitchGuide includes substantial code from both
projects and keeps their full commit history. This repository does not claim
sole authorship of the derived work: copyright in the inherited code remains
with the HachiTune contributors, Session Loops, and the other contributors to
those projects.

PitchGuide is an independent project. It is not affiliated with or endorsed by
Session Loops or the HachiTune contributors, and it does not track upstream
PitchNet changes.

PitchGuide is distributed under the [GNU Affero General Public License, version 3](LICENSE)
(AGPLv3), the same license as PitchNet. The complete license text is included in
this repository. When conveying copies or modified versions, preserve all
applicable copyright, license, and attribution notices, and make the
corresponding source available as required by the AGPLv3. Third-party
components and model assets are listed in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Overview

PitchGuide is a standalone app and audio plugin for editing vocal pitch in a
piano roll. It analyzes audio using neural pitch detectors (FCPE, optionally
RMVPE), segments notes with the GAME model, and resynthesizes edits in real time
through a neural vocoder (PC-NSF-HiFiGAN). Edits are previewed instantly with
minimal latency and exported as audio or MIDI. The plugin supports both ARA
hosts and a non-ARA capture workflow.

### Roadmap

PitchGuide is being reworked step by step from the PitchNet codebase:

1. Rebrand and working CI (done).
2. Time-align a new take to an isolated reference vocal (done).
3. Drop in a reference and a take; show the reference pitch over the take (done).
4. "Match reference" correction in the editor: pitch, expression and timing (done).
5. Drag the corrected take out of the plug-in into the DAW as audio (done).
6. Live pitch detection (YIN) as on-screen feedback while singing: the
   PitchGuide Live karaoke plug-in (done).

## Features

- Piano roll editing with select, split, pitch drawing, and timing modes
- Neural pitch detection (FCPE default, RMVPE optional)
- GAME-based note segmentation with D3PM diffusion model
- Pitch curve editing with per-note delta, scale, offset, tilt, and variance controls
- Real-time incremental vocoder resynthesis (PC-NSF-HiFiGAN)
- High-quality, band-limited audio sample-rate conversion with
  [r8brain-free-src](https://github.com/avaneev/r8brain-free-src)
- Undo/redo for all editing operations
- Export audio (WAV) and MIDI, or drag the corrected take straight into a DAW
- Standalone app plus VST3/AU plugins (AAX optional, ARA supported)
- GPU acceleration via CUDA or DirectML on Windows; CoreML on macOS
- Multi-language UI (English, Japanese, Chinese Simplified, Chinese Traditional, Korean, Spanish)

## Download

Builds are published on the
[GitHub releases page](https://github.com/Fearnus/PitchGuide/releases). Every
push also produces Windows and Linux builds as workflow artifacts under
[Actions](https://github.com/Fearnus/PitchGuide/actions).

On Windows, either run the installer, or copy the portable
`PitchGuide.vst3` folder (it carries its own models) into
`C:\Program Files\Common Files\VST3` and rescan plugins in your DAW.

## Requirements

- CMake 3.22+
- C++17 compiler
- Git (for submodules)

### Platform notes

| Platform | Requirements |
| --- | --- |
| Windows | Visual Studio 2019+ with C++ workload, Windows SDK |
| macOS | Xcode + command line tools |
| Linux | GCC/Clang, ALSA development libraries |

## Models and Assets

The build expects these model files in `Resources/models` and fails if any are missing:

- `pc_nsf_hifigan.onnx` (vocoder; macOS builds need `pc_nsf_hifigan.mlmodelc` instead, which is not in this repository)
- `fcpe.onnx` (pitch detection)
- `GAME/` (note segmentation: `encoder.onnx`, `estimator.onnx`, `segmenter.onnx`, `dur2bd.onnx`, `bd2dur.onnx`, `config.json`), downloaded automatically during CMake configure if missing
- `cent_table.bin`
- `mel_filterbank.bin`

`rmvpe.onnx` is optional and is not shipped: it is used only if present (see
the RMVPE note in `THIRD_PARTY_NOTICES.txt`).

Other runtime assets live in `Resources/lang` and `Resources/fonts`.

## Build

### Submodules

```bash
git submodule update --init --recursive
```

JUCE is the official 8.0.14 release; CMake applies
`third_party/patches/juce-8.0.14.patch` to it when it configures. A checkout
made while the submodule still pointed at the Session Loops fork needs
`git submodule sync` before the update.

### Configure

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

### Build

```bash
cmake --build build --config Release
```

### Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPITCHNET_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

On Linux, install JUCE's build dependencies first (Ubuntu package names):

```bash
sudo apt-get install ninja-build libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
```

### Optional flags

- `-DUSE_CUDA=ON` Enable CUDA execution provider (Windows)
- `-DUSE_DIRECTML=ON` Enable DirectML execution provider (Windows)
- `-DUSE_ASIO=ON` Enable ASIO support (Windows)
- `-DUSE_BUNDLED_CUDA_RUNTIME=ON` Bundle CUDA redistributables (Windows)
- `-DUSE_BUNDLED_DIRECTML_RUNTIME=ON` Bundle DirectML runtime (Windows)
- `-DONNXRUNTIME_VERSION=1.19.2` Override ONNX Runtime version
- `-DPITCHNET_DEV_BUILD=ON` Side-by-side development identity ("PitchGuide Dev", plugin code `Pgd0`, bundle ID `com.fearnus.pitchguide.dev`)
- `-DPITCHNET_BUILD_TESTS=ON` Build the unit tests in `tests/`
- `-DPITCHNET_BUILD_TOOLS=ON` Build the developer tools in `tools/`, e.g.
  `pitchguide-align <reference> <take> [alignment.csv]`, which aligns a take to
  a reference vocal and writes the frame map as CSV. Run from the repository
  root, it also detects pitch in both files, suggests an octave shift, and
  reports how often the take's pitch matches the aligned reference

The `PITCHNET_` prefix on CMake options and internal targets is inherited from
PitchNet and kept so existing build commands keep working.

Notes:
- CUDA and DirectML cannot be enabled at the same time.
- ONNX Runtime is downloaded during CMake configure.

## Plugin Formats

- VST3 and AU are built by default, for both PitchGuide and PitchGuide Live
  (turn the latter off with `-DPITCHNET_BUILD_LIVE=OFF`).
- AAX is built only when `AAX_SDK_PATH` is set and valid.
- ARA is enabled when the `third_party/ARA_SDK` submodule is present.

## Usage

1. Open an audio file (WAV/MP3/FLAC/OGG).
2. Analyze to detect pitch and segment notes.
3. Edit notes in the piano roll — adjust pitch, timing, and expression.
4. Preview changes in real time via the neural vocoder.
5. Export the edited audio or MIDI, or drag the take out of the toolbar into a DAW.

### Guiding a take with a reference vocal

Drag audio files onto the editor (standalone, or the plug-in in a non-ARA host
such as FL Studio). While dragging, the window splits into two targets:

- **Reference**: the isolated vocal to match. It is assumed to start at song
  time 0:00.
- **Take**: your recording. In the plug-in it is placed at song time 0:00 and
  replaces any captured take; capture by playback still works too.

Once both are loaded, the reference pitch is drawn in amber over the take,
aligned to the take's timing and shifted by whole octaves to the octave you
sang in. A take may cover only part of the reference, such as one verse. The
reference is kept when you load another take and is saved with the project
(and with the host project in the plug-in).

To correct the take, click the target button in the toolbar or choose
**Edit > Match Reference...** (Ctrl+M / Cmd+M). It works on the selected notes,
or on every note when none is selected, and previews as you change:

- **Note pitch** (default 100%): moves each note's centre to the pitch the
  reference sings over it.
- **Expression** (default 0%): blends the note's slides and vibrato toward the
  reference's, around the note's new centre.
- **Timing** (default 0%): moves each note's start and end toward where the
  reference sings them, so a late or rushed line lands on the reference's
  timing. Notes never overlap, and the breaths before and after each phrase
  move with it, as with the Timing tool.
- **Transpose** (-12 to +12 semitones): added on top of the automatic octave
  shift, for singing the part in another range.

Notes the reference is silent over are left alone. OK applies the settings
shown as one undo step; Cancel puts the notes back.

The popup opens with the settings you last confirmed (saved with the project)
and changes nothing until you move a slider. Reopening it adjusts the earlier
match instead of adding to it: lowering Expression from 50% to 20% gives 20%,
measured from the notes as you sang them. A note you edited by hand after
matching starts again from your edit. **Edit > Remove Reference** stops
guiding the take.

### Dragging the corrected take into the DAW

When the take sounds right, drag the handle at the right of the toolbar (a
waveform with an arrow, next to the side-panel button) and drop it on the
DAW, for example on FL Studio's playlist. What lands there is a WAV of the
corrected take with every edit rendered. The plug-in's Dry/Wet and Output
Gain host parameters are mixing controls and are not applied to it.

- While edits are still rendering the handle is dimmed, and a drag only says
  so. Drag again once the progress line under the edit tools is gone.
- The file is named after the take, `<take> (PitchGuide).wav`, and saved in
  `Documents/PitchGuide/Takes` (or `PitchGuide Takes` in the system temp
  folder if Documents cannot be written to). It stays there because a DAW
  plays a dropped file from where it lies: FL Studio does not copy it into
  the project. Clear the folder out yourself once a song is finished and
  bounced.
- Dragging again without new edits reuses the same file. After more edits
  the next drag writes `<take> (PitchGuide 2).wav`, and so on, so a clip
  already in the DAW never changes underneath it.
- The file starts at song time 0:00, like the take: a dropped take file
  starts there, and a captured take keeps the silence before the point where
  capture began. Drop it at the very start of the song and it lines up.
- It is a 24-bit WAV at the project's sample rate, mono when the take is.

### Singing along with PitchGuide Live

PitchGuide Live is a second, separate plug-in for karaoke practice. It has
none of the editor: it shows what to sing and how close you are, live.

1. Put **PitchGuide Live** on the track your microphone comes in on, and turn
   on input monitoring so the track processes the mic while the song plays.
   The voice passes through unchanged.
2. Drop an isolated vocal of the song (a stem) on its window, or click
   **Load reference...**. Its notes are found by the same analysis the editor
   uses for a take (FCPE or RMVPE pitch and GAME notes, on the device chosen
   in PitchGuide's settings), once; they are saved with the DAW project, so
   reopening it does not analyse the song again.
3. Press play in the DAW. The notes scroll towards the line on the left, the
   reference's own pitch is drawn over them in amber, and your voice is drawn
   behind the line: green when it is in tune with the reference, yellow when
   close, red when off, purple between notes. A dot on the line shows the
   pitch you are singing now and how many cents you are off. With the
   transport stopped it works as a tuner against the note under the playhead.
4. **Transpose** (-24 to +24 semitones, also a host parameter) moves the
   reference into your range, for example -12 to sing a part an octave down.
5. The score is the share of the time you sang during the reference's notes
   that was in tune. "In tune" means within the tolerance of either the note
   or what the reference actually sings there, so copying the singer's slides
   and vibrato counts. Loop a passage in the DAW and each attempt overwrites
   the last; **Clear** starts over.

**Settings** holds the rest: where the reference starts on the host timeline
(the reference starts at 0:00 by default; put the playhead where the song
starts and press **Use playhead** if it starts elsewhere), sync compensation
for your interface's round-trip latency, the in-tune tolerance (default 50
cents), and a microphone gate with a live input level. The mouse wheel zooms
time; double-click resets it.

Pitch is detected with YIN on the input decimated to about 16 kHz, a 5 ms hop
and a window just long enough for 65 Hz: the detector reports a sound about
20 ms after it was sung, using well under 1% of one CPU core.

### Plugin modes

- **ARA mode**: Direct audio access in supported hosts (e.g., Studio One, Cubase, Logic).
- **Non-ARA mode**: Auto-capture and process in hosts without ARA.

## Project Structure

```
PitchGuide/
  Source/
    Audio/        # Audio engine, pitch detection, synthesis, vocoder
      Live/       # Real-time pitch detection and scoring (PitchGuide Live)
    Models/       # Project, note, and audio data models
    UI/           # Piano roll, toolbar, and UI components
    Utils/        # Resampling, localization, mel spectrogram utilities
    Undo/         # Undo/redo action system
    Plugin/       # VST3/AU/AAX/ARA plugin integration
    LivePlugin/   # PitchGuide Live, the karaoke plug-in
  Resources/
    models/       # ONNX models and data files
    lang/         # Localization JSON (en, ja, zh, zh-TW)
    fonts/        # UI fonts
    svg/          # Toolbar icons
  third_party/
    JUCE/             # JUCE 8.0.14 (submodule, official release)
    patches/          # Changes CMake applies to JUCE when configuring
    ARA_SDK/          # ARA SDK (submodule, optional)
    r8brain-free-src/ # High-quality audio resampler (submodule)
  CMakeLists.txt
```

## Acknowledgements

PitchGuide builds on the work of the [HachiTune](https://github.com/KCKT0112/HachiTune)
contributors and of [Session Loops](https://github.com/SessionLoops/PitchNet),
whose PitchNet is the codebase this project continues from.

This project uses the following models from the open-source community:

- [GAME](https://github.com/openvpi/GAME) — Note segmentation model by [OpenVPI](https://github.com/openvpi)
- [PC-NSF-HiFiGAN](https://github.com/openvpi/vocoders) — Neural vocoder by [OpenVPI](https://github.com/openvpi)
- [FCPE](https://github.com/CNChTu/FCPE) — Pitch estimation model by [CNChTu](https://github.com/CNChTu)
- [RMVPE](https://github.com/yxlllc/RMVPE) — Robust pitch estimation model by [yxlllc](https://github.com/yxlllc)

Audio sample-rate conversion uses
[r8brain-free-src](https://github.com/avaneev/r8brain-free-src) by Aleksey
Vaneev under the MIT License.

## License

See `LICENSE`.
