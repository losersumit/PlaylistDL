# PlaylistDL

Windows CMD YouTube playlist downloader written in C++17. Uses yt-dlp for extraction and FFmpeg for merging separate streams.

## Features
- Paste a YouTube video or playlist URL.
- Creates a folder under the Windows Downloads directory named after the playlist.
- Selects best available video + audio streams with `bv*+ba/b`; best exposed resolution/FPS is selected where available.
- Leaves the output container to yt-dlp/FFmpeg rather than forcing MP4.
- Configurable parallel video downloads (1–20, default 5).
- CMD dashboard with per-video progress, speed, ETA, and overall completion.
- Numbered filenames preserve playlist order.
- Resumes partial files where supported; completed files are not overwritten.

## Requirements
- Windows 10/11 x64.
- CMake 3.20+.
- Visual Studio 2022 Build Tools with **Desktop development with C++** (or another C++17 toolchain).
- `yt-dlp.exe` and `ffmpeg.exe` available on PATH.

Get yt-dlp from https://github.com/yt-dlp/yt-dlp/releases and FFmpeg from https://ffmpeg.org/download.html. Add their executable folder to PATH, open a new terminal, and verify `yt-dlp --version` and `ffmpeg -version`.

## Build
Open **x64 Native Tools Command Prompt for VS 2022** in this directory and run:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Or run `build.bat`.

Executable: `build\Release\PlaylistDL.exe`.

## Notes
- The worker count limits concurrent yt-dlp video processes; FFmpeg may run within an active worker for merging.
- Progress can be approximate for fragmented or unknown-size downloads.
- Failed items are reported at the end; rerun the playlist to retry. Successful files are not overwritten.
- Ctrl+C stops the program. Partial downloads may resume on a later run.
- This version does not import browser cookies or bypass access restrictions.
- Use only for videos you own or are otherwise authorized to download; respect copyright, YouTube's terms, and applicable law.
