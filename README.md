# Auctrum

> A cross-platform real-time music player and visualizer built with SDL3, libsndfile, and KissFFT.

## 📸 Preview

![Auctrum gif](docs/auctrum.gif)

## ✨ Features

- Real-time waveform and spectrum visualization
- Multiple visualization modes
- Adjustable playback speed
- Looping and seek controls
- Cross-platform (Windows, Linux, macOS)

## 🚀 Getting started
Pre-built binaries are available for **Windows**.
Simply download the executable and run it.

For **Linux** and **macOS**, the project must be built from source.
See the build instructions below.

### ⚙️ Dependencies
- SDL3
- SDL_image
- SDL_ttf
- Libsndfile
- KissFFT

### 📦 Build
To build this project, modify the `CMakeLists.txt` file to provide paths to your installation of the above dependencies.  
Then build with the following commands: 

```bash
mkdir build
cd build
cmake ..
ninja
```
> **Windows users:** After building the project, be sure to copy the required DLL files into the `build/bin`
directory (where the executable resides) for the application to run correctly.

## 🔧 Configuration
Auctrum includes a configuration header file at `src/config.h` that allows users to customise various compile-time parameters to their preferences:

- FFT size
- Number of spectrum bars
- Window width and height
- Window opacity
- Other audio playback and visualisation parameters

These options are intended for users who want to modify the visualizer’s behavior or appearance at build time.
If you change any values, simply rebuild the project.

## 💡 Usage

1. Launch the application.
2. Press **E** to open an audio file.
3. Select a supported format (WAV, FLAC, OGG, MP3, ...).

Alternatively, you can also provide an audio file via the command line:
```
ac.exe <audio file>
```

### 🎮 Controls

| Key                            | Action                          |
|--------------------------------|---------------------------------|
| `E`                            | Open audio file                 |
| `Space`                        | Play / Pause                    |
| `M`                            | Mute audio                      |
| `L`                            | Enable loop                     |
| `V`                            | Toggle visualization modes      |
| `R`                            | Toggle rainbow mode             |
| `T`                            | Toggle timestamp                |
| `A` / `D`                      | Slow down / Speed up audio      |
| `←` / `→`                      | Seek backward / forward         |
| `Ctrl` + `←` / `Ctrl` + `→`    | Seek to start / end             |
| `↑` / `↓`                      | Increase / Decrease volume      |
| `Q`                            | Exit application                |

### 🎵 Supported Audio Formats

This project supports all audio formats supported by **libsndfile**, including both uncompressed and compressed formats where supported by your libsndfile build.

| Format        | Description                           | File Extensions              |
|---------------|---------------------------------------|------------------------------|
| WAV           | Waveform Audio File Format            | `.wav`                       |
| AIFF / AIFC   | Audio Interchange File Format         | `.aif`, `.aiff`, `.aifc`     |
| AU / SND      | NeXT/Sun audio file                   | `.au`, `.snd`                |
| PAF           | Ensoniq Paris                         | `.paf`                       |
| SVX           | Amiga IFF 8SVX format                 | `.iff`, `.svx`               |
| NIST (Sphere) | NIST SPHERE audio format              | `.nist`, `.nss`, `.nvf`      |
| VOC           | Creative Labs VOC format              | `.voc`                       |
| IRCAM         | Institut de Recherche et Coordination Acoustique/Musique | `.sf`           |
| W64           | Sonic Foundry 64-bit RIFF             | `.w64`                       |
| MAT4 / MAT5   | Matlab (Mathworks) format             | `.mat`                       |
| PVF           | Portable Voice Format                 | `.pvf`                       |
| XI            | Fasttracker 2 Instrument file         | `.xi`                        |
| HTK           | Hidden Markov Model Toolkit file      | `.htk`                       |
| SDS           | MIDI Sample Dump Standard             | `.sds`                       |
| AVR           | Audio Visual Research                 | `.avr`                       |
| WAVEX         | Extended WAV                          | `.wav`                       |
| SD2           | Sound Designer 2                      | `.sd2`                       |
| FLAC          | Free Lossless Audio Codec             | `.flac`                      |
| OGG (Vorbis)  | Ogg Vorbis audio                      | `.ogg`                       |
| MP3           | MPEG-1/2 Audio Layer III              | `.mp3`                       |
| Opus          | Opus audio codec                      | `.opus`                      |

> **Note:** Support for compressed formats such as **MP3**, **OGG (Vorbis)**, and **Opus** relies on using a libsndfile build that includes the necessary codec support.

## ❓ FAQ / Troubleshooting

- **Q:** The application doesn't start / crashes.  
  **A:** Make sure all DLLs / dependencies are correctly installed and copied (Windows).

- **Q:** Unsupported audio format error.  
  **A:** Verify your libsndfile build supports the format or convert audio to WAV/FLAC.

- **Q:** How can I change visualization modes?  
  **A:** Press the `V` key during playback.

## 🙏 Credits

Auctrum is powered by these excellent open-source libraries; make sure to check them out:

- [SDL3](https://libsdl.org/)
- [SDL_image](https://github.com/libsdl-org/SDL_image)
- [SDL_ttf](https://github.com/libsdl-org/SDL_ttf)
- [libsndfile](https://libsndfile.github.io/libsndfile/)
- [KissFFT](https://github.com/mborgerding/kissfft)
