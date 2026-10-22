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
4. "Match reference" pitch correction in the editor (pitch done; timing next).
5. Live pitch detection (YIN/MPM) as on-screen feedback while singing.

## Features

- Piano roll editing with select, split, pitch drawing, and timing modes
- Neural pitch detection (FCPE default, RMVPE optional)
- GAME-based note segmentation with D3PM diffusion model
- Pitch curve editing with per-note delta, scale, offset, tilt, and variance controls
- Real-time incremental vocoder resynthesis (PC-NSF-HiFiGAN)
- High-quality, band-limited audio sample-rate conversion with
  [r8brain-free-src](https://github.com/avaneev/r8brain-free-src)
- Undo/redo for all editing operations
- Export audio (WAV) and MIDI
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

- VST3 and AU are built by default.
- AAX is built only when `AAX_SDK_PATH` is set and valid.
- ARA is enabled when the `third_party/ARA_SDK` submodule is present.

## Usage

1. Open an audio file (WAV/MP3/FLAC/OGG).
2. Analyze to detect pitch and segment notes.
3. Edit notes in the piano roll — adjust pitch, timing, and expression.
4. Preview changes in real time via the neural vocoder.
5. Export the edited audio or MIDI.

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
- **Transpose** (-12 to +12 semitones): added on top of the automatic octave
  shift, for singing the part in another range.

Notes the reference is silent over are left alone. OK keeps the result as one
undo step; Cancel puts the notes back. **Edit > Remove Reference**
stops guiding the take.

### Plugin modes

- **ARA mode**: Direct audio access in supported hosts (e.g., Studio One, Cubase, Logic).
- **Non-ARA mode**: Auto-capture and process in hosts without ARA.

## Project Structure

```
PitchGuide/
  Source/
    Audio/        # Audio engine, pitch detection, synthesis, vocoder
    Models/       # Project, note, and audio data models
    UI/           # Piano roll, toolbar, and UI components
    Utils/        # Resampling, localization, mel spectrogram utilities
    Undo/         # Undo/redo action system
    Plugin/       # VST3/AU/AAX/ARA plugin integration
  Resources/
    models/       # ONNX models and data files
    lang/         # Localization JSON (en, ja, zh, zh-TW)
    fonts/        # UI fonts
    svg/          # Toolbar icons
  third_party/
    JUCE/             # JUCE framework (submodule)
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
