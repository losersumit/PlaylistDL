# PlaylistDL

A Windows CMD YouTube video and playlist downloader written in C++17. It uses [yt-dlp](https://github.com/yt-dlp/yt-dlp) to retrieve video information and download streams, and FFmpeg to merge separate audio/video streams when needed.

## Features

- Accepts a public YouTube video or playlist URL.
- Creates a folder in your Windows **Downloads** directory using the playlist title.
- Selects the best available video and audio streams with `bv*+ba/b`, including the best available resolution and frame rate exposed by YouTube.
- Preserves the output container selected by yt-dlp; it does not force MP4 conversion. Downloads may therefore be `.webm`, `.mp4`, or another supported container.
- Configurable concurrent downloads (1–20, default 5).
- CMD dashboard with per-video progress, speed, ETA, and overall completion.
- Numbered filenames preserve playlist order.
- Resumes partial downloads where supported; completed files are not overwritten.

## Requirements

- Windows 10/11 x64.
- CMake 3.20 or newer.
- A C++17 compiler and a compatible build tool. The setup below uses **GCC (MSYS2 UCRT64) + MinGW Make**.
- `yt-dlp.exe`.
- `ffmpeg.exe` (recommended/required when separate streams need merging).

### Install the build tools

1. Install [CMake](https://cmake.org/download/) and add it to PATH.
2. Install [MSYS2](https://www.msys2.org/).
3. Open the **MSYS2 UCRT64** terminal and install GCC and Make:

   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
   ```

4. Add `C:\\msys64\\ucrt64\\bin` to your Windows PATH.
5. Open a new PowerShell window and verify:

   ```powershell
   g++ --version
   mingw32-make --version
   cmake --version
   ```

A different C++17 toolchain can also be used, but the commands below are for MSYS2 UCRT64.

## Build with GCC / MinGW

Open PowerShell in the cloned PlaylistDL repository directory.

Configure the project:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++
```

Compile:

```powershell
cmake --build build
```

The executable is created at:

```text
build\\PlaylistDL.exe
```

To rebuild from a clean configuration, remove the existing build directory first:

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
```

Then run the configure and build commands again. On the tested MSYS2 Make setup, use `cmake --build build` without the `--parallel` option; the parallel invocation may fail with an output-sync mutex error.

## Install runtime dependencies

Download the latest Windows executable for [yt-dlp](https://github.com/yt-dlp/yt-dlp/releases/latest) and place `yt-dlp.exe` in the same directory as `PlaylistDL.exe` (inside `build`) or make it available on PATH.

Install [FFmpeg](https://ffmpeg.org/download.html) and add the directory containing `ffmpeg.exe` to PATH. Open a new terminal and verify:

```powershell
yt-dlp --version
ffmpeg -version
```

You can also test the local yt-dlp copy directly:

```powershell
.\\build\\yt-dlp.exe --version
```

## Run

From the repository directory:

```powershell
.\\build\\PlaylistDL.exe
```

Paste a YouTube video or playlist URL when prompted, then enter the number of simultaneous downloads (1–20, or press Enter for the default of 5).

Downloaded files are saved under:

```text
%USERPROFILE%\\Downloads\\<Playlist Name>\\
```

## Notes

- The worker count limits concurrent yt-dlp video processes; FFmpeg may run within an active worker when streams need merging.
- Progress can be approximate for fragmented downloads or streams with unknown sizes.
- Failed items are reported at the end. Run the playlist again to retry; successful files are not overwritten.
- Ctrl+C stops the program. Partial downloads may resume on a later run.
- This version does not import browser cookies or bypass access restrictions.
- Use only for videos you own or are otherwise authorized to download; respect copyright, YouTube's terms, and applicable law.
