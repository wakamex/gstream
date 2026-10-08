# Decisions

Decisions made while carrying out `PLAN.md`, with the reason for each. Measurements the plan asks to record are kept here too, under Measurements.

## Working setup

- `PLAN.md` stays uncommitted, as asked. This file records what the plan says to record "in this file".
- Nothing is pushed. gesso, gtube and streamit commits stay local, so the GitHub CI workflows the plan calls for are written but have not run. Local runs on the reference machines stand in for them.
- streamit depends on gesso by path (`../gesso`) while gesso's commits are local; it switches to a pinned URL when gesso is pushed. gtube is built against local gesso with `zig build --fork=../gesso` for the same reason.
- streamit's CI workflows are written (M10) but would fail on the path dependency until gesso is pushed and pinned by URL; `RELEASES.md` lists that step. gesso's own workflow is in place.
- gtube's working tree holds another session's uncommitted Bend work (`src/bend/`, `src/viz.c`, `README.md`). Commits in gtube stage only the files this work changes.
- gesso's working tree holds an uncommitted README edit (the Principles section) from another session. gesso commits stage only the files this work changes, and README additions for new modules are made around that edit.

## Reference machines

| System | Machine | GPU | Hardware H.264 decoding |
|---|---|---|---|
| Linux | this host (Fedora 44) | NVIDIA RTX 3090, proprietary driver 615.71 | VAAPI through `libva-nvidia-driver` |
| Linux | hpbook (Fedora 44 laptop) | AMD Renoir (Radeon Vega), Mesa 26.1 | none: Fedora's Mesa is built without H.264 (`No support for codec h264 profile 100`); needs `mesa-va-drivers-freeworld` from RPM Fusion |
| Windows | the Windows PC | NVIDIA RTX 3080 | Direct3D 11 |
| macOS | none reachable | | |

macOS code paths are written to the platform's documented APIs but cannot be built or run here: gesso's macOS builds need a Mac with Xcode, and no Mac is reachable over SSH.

## Decisions

### Twitch segments come in two formats (2026-10-08)

The plan assumed transport stream segments. Live captures show both: lirik, ohnepixel and nickmercs serve H.264 Main profile renditions as fragmented MP4 (`EXT-X-MAP` initialization segment, `mp42` brand, AAC-LC at 48 kHz), while jynxzi serves H.264 High profile as transport stream. gesso gets a fragmented MP4 demuxer, `gs_mp4`, beside `gs_ts`; both emit H.264 in Annex B form and raw AAC frames, so `gs_video` and `gs_aac` take one input format each. `PLAN.md` is updated to match.

### FFmpeg 9.0.2, generated once per target (2026-10-08)

FFmpeg 9.0.2 (2026-09-18) is the newest release. `ffmpeg/tools/import.sh` in gesso runs its configure with zig cc for Linux x86-64, Windows x86-64 and macOS aarch64 and stores the generated files and source list; `build.zig` compiles 249, 249 and 232 files respectively. NASM comes from `allyourcodebase/nasm`, built by zig for the build host, only when an x86 target needs it. Two workarounds in the import: zig's glibc headers lack `sys/sysctl.h`, so `HAVE_SYSCTL` is cleared on Linux, and small wrappers drop linker flags zig rejects during configure's checks (`--pic-executable` for MinGW, `-dynamic` for macOS).

The macOS configuration cannot run VideoToolbox's configure checks without Apple's SDK, so the import enables VideoToolbox in the generated files afterwards, as `--enable-videotoolbox` does with a current SDK. Unverified until built on a Mac.

### VAAPI without libdrm (2026-10-08)

FFmpeg maps VAAPI frames to DRM descriptors only when built with libdrm. `gs_video` calls `vaExportSurfaceHandle` itself instead, so the Linux build needs only libva and libva-drm, which `ffmpeg/va_loader.c` loads at run time. libva's public headers (MIT) are vendored in `vendor/va`, 22 files.

### VAAPI on NVIDIA through its VAAPI bridge (2026-10-08)

NVIDIA's VAAPI driver (`libva-nvidia-driver` 0.0.18 on this host) decodes through NVDEC and exports dma-bufs that EGL imports, so `gs_video` treats it like any other VAAPI driver. Measured with `zig build bench-video` on the 1080p60 capture, offscreen: VAAPI with EGL display 22% of one core, software decoding 54%, the same picture in both. An earlier hang came from forcing the driver's direct backend (`NVD_BACKEND=direct`), not from the driver's default.

### Renderer and decoding path per platform (M1, 2026-10-08)

| System | Renderer | Decoding | Fallback |
|---|---|---|---|
| Linux | `opengles2` (EGL forced) when a render node's VAAPI driver decodes H.264 High profile | VAAPI, dma-bufs imported through EGL | SDL's default renderer and software decoding |
| Windows | `direct3d11`, with `SDL_HINT_RENDER_DIRECT3D_THREADSAFE` | Direct3D 11 on the renderer's device, copied on the GPU | software decoding on the same renderer |
| macOS | `metal` | VideoToolbox pixel buffers | software decoding (unverified) |

`gs_video_prepare` makes the choice before the window exists. SDL creates its Direct3D 11 device single-threaded unless asked; with decoding on its own thread that produced `Failed to add bitstream or slice control buffer` and a hang, fixed by the hint. NVIDIA on Linux takes the VAAPI path through `libva-nvidia-driver`; no NVDEC-specific path is needed.

### Parity list (M0, 2026-10-08)

Every control and feature of the Qt build (`app/Main.qml` at `qt-final`), and what the C build does with it.

| Qt build | C build |
|---|---|
| Sign-in card: "Sign in with Twitch", the device code, "Open page", "Cancel" | kept |
| Account button with a popup: "Signed in as", token profile, "Sign out" | kept, in `gs_ui`'s floating layer, without the token profile line |
| "Search follows" filter field | kept |
| Sort dropdown: Viewers, Channel, Category, Started | kept, a dropdown in the floating layer |
| "18+" checkbox for mature channels | kept, a toggle |
| Shown/total count beside the filters | kept |
| Cards or compact rows, a button with a tooltip | kept |
| "Couldn't refresh" warning with the last update time | kept |
| Empty states: no channels live, none match the filters | kept |
| Channel card: thumbnail, name, viewers, title, category, state; click to play | kept |
| Player bar: source label, Stop, Mute, volume slider with percentage, Full/Window | kept |
| Fullscreen player with the same controls; Esc leaves fullscreen | kept |
| Settings: Twitch Client ID field and Save | replaced: the built-in client ID, overridable with `--client-id` |
| Token storage dropdown | dropped: one storage, `gs_secret` |
| Diagnostics panel (Qt, client ID, token storage, resolver, libmpv) | replaced: the diagnostics overlay (renderer, decoding path, buffer, dropped frames, clock error, memory, CPU) |
| Dev playback mode (`STREAMIT_DEV_TWITCH_CHANNEL`) | replaced: `--play CHANNEL` |

### Text: kb_text_shape, and which emoji draw in colour (M4, 2026-10-08)

The shaper is kb_text_shape (zlib licence, one C header) rather than HarfBuzz, which is C++. It adds about 620 KB to the Windows executable (125 KB compressed). Two bugs in it are fixed in gesso's copy and listed in `vendor/kb/VERSION`. gesso chooses fonts per grapheme itself: kb's own fallback cannot be told to prefer an emoji font, and a font pushed between segments took effect one segment late.

Colour emoji draw from COLR version 0 layers (Windows), CBDT bitmaps (Noto Color Emoji on Debian, Ubuntu, Arch) and sbix bitmaps (macOS, tested only with a generated font). COLR version 1's gradients and transforms are not drawn: on Fedora 43 and later, whose only colour emoji font is COLRv1, emoji show in monochrome from Noto Emoji. Drawing COLRv1 would need a paint-graph renderer with gradients and affine outline rasterization, several hundred lines; it waits for a user on such a system to ask.

Known gaps: on Fedora, Korean shows as boxes, because its only Hangul font is a CFF2 variable font, which stb_truetype cannot read (a CFF2 charstring reader would fix it); in Windows' Segoe UI Emoji, emoji ZWJ sequences such as families draw as their separate members, because kb_text_shape matches that font's ligatures without the joiner glyph; Windows has no flag emoji, as in Windows' own apps.

### Parity details (M9, 2026-10-08)

- Quality: the player bar's dropdown lists Best and the channel's renditions, best first, from the master playlist of the last resolve; choosing one restarts the channel at it. The choice holds for later channels in the session and is not saved between runs: the Qt build had no quality setting to match.
- Errors: each `twitch_error` class has its own sentence, with Twitch's words added for the unsupported, network and unknown classes; a failed or ended stream offers Try again.
- The diagnostics panel became the F1 overlay, also in the account menu. The About card names the version (from `build.zig.zon`), FFmpeg's release (`gs_video_library`), SDL's version and every library's licence. It replaces the whole view while open, since `gs_ui` has no modal layer to keep clicks from the widgets beneath.
- Full and F show the player alone and full screen, as the Qt build's Full did; Esc and Stop leave both. `--play` still opens the player-only view in a window.

### Video with unstated colours (2026-10-08)

Twitch's H.264 streams leave the colour matrix, primaries and range unstated. SDL's software renderer refuses such a texture (`Unsupported YUV colorspace`), so with it no picture showed. `gs_video` assumes what players do: BT.709 at 720 lines and up, BT.601 below, limited range.

### Releases: Linux and Windows, macOS built in CI (M10, 2026-10-08)

v0.1.0 ships Linux (glibc 2.27) and Windows archives, as gtube does. macOS waits: no Mac is reachable to run a build, and signing and notarisation need an Apple developer account the project does not have. `.github/workflows/macos.yml` builds and tests on macOS once pushed, and gesso's workflow runs its video tests there; an unsigned `.app` can follow once a macOS run has played a stream.

Each release attaches a third asset with FFmpeg's unmodified release tarball (checked against its SHA-256) and gesso's FFmpeg build files, following FFmpeg's licence checklist for a static link. glibc 2.27 needs a stand-in for `posix_spawn_file_actions_addchdir_np`, which SDL links; it moved from gtube into gesso so every app gets it. gtube keeps its own copy until its gesso pin includes gesso's, which is harmless because the linker takes gesso's only when nothing else defines the function. The glibc 2.27 build starts and renders in an Ubuntu 18.04 container.

## Measurements

### M0: the Qt build (2026-10-08)

| Measure | Value |
|---|---|
| Linux AppImage | 140 MB (134 MiB) |
| Linux unpacked AppDir | 369 MB |
| Windows package folder (`out\windows\StreamIt`) | 195 MB |
| Windows, playing lirik live (1080p60), libmpv as shipped (software decoding) | 106% of one core, private working set 434 MB average, 453 MB peak |

Measured on the Windows PC in its desktop session with `STREAMIT_DEV_TWITCH_CHANNEL=lirik`, sampling for 20 s after 20 s of start-up. The Qt build has no `hwdec` setting, and libmpv reads no configuration file, so the `hwdec=auto-safe` comparison the plan names would need a rebuilt Qt app; the hardware milestone compares against the shipped figure and against `gs_video`'s own software path instead. Browsing memory for the Qt build is not measured: its demo channel list (`STREAMIT_UI_FIXTURE`) is compiled out of release builds.

### M1: decoding paths (2026-10-08)

`zig build bench-video -Dvideo -- lirik-1080p60.h264`: a 75 s local capture of lirik at 1080p60 (H.264 Main, about 6.9 Mbit/s), looped for 20 s, with the stats overlay drawn each frame. CPU is the share of one core over the run; memory is the figure `gs_stats` reports (private working set on Windows, proportional set size on Linux).

| Machine | Path | CPU | Memory | Shown, dropped |
|---|---|---|---|---|
| Windows PC, RTX 3080, desktop | Direct3D 11 | 18.6% | 41 MB | 1155, 61 |
| Windows PC, RTX 3080, desktop | software | 51.0% | 64 MB | 1185, 24 |
| this host, RTX 3090, offscreen | VAAPI (NVIDIA driver) | 28.4% | 228 MB | 684, 44 (12 s) |
| this host, RTX 3090, offscreen | software, `opengl` | 55.3% | 143 MB | 705, 24 (12 s) |
| hpbook, Ryzen 7 4700U, offscreen | software, `opengl` | 89.1% | 96 MB | 840, 72 (15 s) |

The Linux figures carry NVIDIA's driver: its CUDA context for VAAPI adds about 85 MB of private memory, which the 100 MB playing budget cannot absorb on that hardware. The bench executable, with libavcodec, SDL and gesso, is 5.6 MB for Windows in ReleaseFast. Dropped frames come from a 60 fps stream on a 60 Hz present loop: when two frames fall due in one refresh, one is skipped; M7's smoothness work addresses that.

### M2: the empty app (2026-10-08)

The skeleton (`src/main.c`: window, text, the stats overlay, `--shot`, `--stats`) links gesso with `-Dvideo`, so FFmpeg is in it from the start.

| Measure | Linux x86-64 | Windows x86-64 |
|---|---|---|
| Executable, ReleaseFast, stripped | 5.64 MB | 5.61 MB (1.72 MB with xz -9) |
| Memory, idle, after 6 s | 162 MB on this host (NVIDIA's GL driver in the process) | 14.8 MB private working set |

Idle CPU is 9.7% of a core on Windows because every frame is redrawn at the display rate; an idle interface that redraws only when something changes is part of the browsing milestone.

### M5: browsing (2026-10-08)

Layout is gesso's own rectangle cutting, not Clay. For the channel list's layout (a sidebar with 100 cards, a main pane, a dropdown), Clay v0.14 added 142 KB to a Windows executable and needs a 754 KB arena at 1,024 elements, and it is a 4,393-line dependency; `gs_ui`'s layout is a few cut functions in its own file. Clay is zlib-licensed and 4,393 lines in its latest release, not the MIT and 5,058 lines the plan stated.

| Measure, Windows PC, `--demo` (100 channels with thumbnails) | Value |
|---|---|
| Memory, idle | 21.7 MB private working set |
| CPU, idle (the window waits for events) | 0.39% of one core |
| Memory, drawing every frame (`--stats`) | 23.0 MB |
| Executable, ReleaseFast | 6.24 MB |

Not checked on real hardware: display scale 2 (no high-DPI display is attached to the reference machines; `gs_ui` takes its scale from the window's pixel density), and Japanese typed through a system input method (no keyboard at the machines; the field's composition handling is tested with synthetic input-method events in gesso's tests).

### M6: an hour of live audio (2026-10-08)

Each machine played a live channel audio-only for 3,600 s with `--stats`, which redraws every frame, so CPU here includes drawing the window 60 times a second.

| Machine, channel, sound output | Stalls, skips, renewals | Behind the live edge | CPU | Memory |
|---|---|---|---|---|
| Windows PC, jynxzi (transport stream), the desktop's sound card | 0, 0, 0 | 5 to 7 s throughout | 11.8% | 8 to 24 MB, 11.9 MB mean |
| this host, lirik (fragmented MP4), PipeWire null sink | 0, 0, 0 | 7 to 34 s | 9.1% | 37 to 186 MB (proportional set size, falling as shared pages were reclaimed) |
| hpbook, nickmercs (fragmented MP4), PipeWire null sink | 81 stalls, 0, 0 | 1 to 6 s | 8.3% | 57.7 MB mean (proportional set size) |

The Windows run shows the player itself: the audio clock stayed within 0.7 s of wall time for the hour. Both Linux machines, reached over SSH with no desktop session, play into PipeWire's `auto_null` sink, which a timer drives. On this host, loaded by two Jellyfin transcodes, it ran up to 1.4% slow and later caught up, so the player fell behind and recovered without skipping. On hpbook it ran 0.5% fast, so the player used up its 6 s head start in 12 minutes and then stalled 81 times, about every 35 s, each stall refilling only the 1 s prebuffer. gs_live now refills the starting margin after a stall instead, once per stall. Against gesso's `tests/streams/live.py` publishing 2% slower than real time, which reaches the live edge on demand, 15 minutes stalled 368 times before the change and 4 times after it, once every 194 s. No run needed a renewal or met an ad: each media playlist URL kept working for the whole hour, and none of the three channels ran an ad that reached this player.

### M7: 30 minutes of 1080p60 in software (2026-10-08)

The Windows PC played a synthetic 1080p60 live stream for 1,800 s with software decoding on the Direct3D 11 renderer: H.264 at about 7 Mbit/s from a local looping playlist served from this host through an SSH tunnel, with a discontinuity each minute where the stream's timestamps restart. The desktop was in use during the run.

| Measure | Value |
|---|---|
| Frames shown, dropped | 107,788, 118 (0.11%): 39 in the first 11 s, then bursts of two or three, with none from minute 11 to minute 20 |
| Dropped per minute after start-up | 2.7 |
| Clock error, each second's mean | 6.0 ms mean, 2 to 18 ms; the largest single frame 23 ms late |
| Jitter (time between new frames against their timestamps) | 0.7 ms mean |
| Discontinuities, stalls, skips | 29, 0, 0 |
| CPU, memory | 36.4% of one core, 91 MB mean, 102 MB peak |

The clock error does not grow across discontinuities: it wanders with the phase between the 60 fps stream and the display's refresh, which one frame per refresh cannot hide entirely. The drops after start-up come in bursts that match no discontinuity and are consistent with other work on the desktop.

### M8: hardware decoding on real channels (2026-10-08)

Each run played lirik at 1080p60 (fragmented MP4, about 7 Mbit/s) with `--stats`.

| Machine and path | Length | Frames shown, dropped | Clock error, jitter (means) | CPU | Memory |
|---|---|---|---|---|---|
| Windows PC, Direct3D 11 (`d3d11va`), the desktop in use | 1,200 s | 71,913, 17 (0.85 a minute) | 7.6 ms, 0.76 ms | 12.2% | 42 MB mean, 58 MB peak |
| this host, VAAPI on NVIDIA through EGL, offscreen | 600 s | 35,235, 919 | | 14.6% | 254 MB mean |
| Windows PC, the Qt build (libmpv, software), from M0 | | | | 106% | 434 MB |

On Windows the hardware path meets M7's figures on a live channel and uses an eighth of the Qt build's CPU and a tenth of its memory. The Qt build cannot be switched to `hwdec=auto-safe`, so the comparison is with what it shipped.

The Linux run confirms the app takes the VAAPI path and decodes with little CPU; its drops are not a smoothness figure, because the offscreen driver has no vsync and paces with a timer, and the host was running two Jellyfin transcodes at the time. Its memory is NVIDIA's CUDA context, as M1 found. hpbook has no H.264 VAAPI driver (Fedora's Mesa lacks the codec), and no Mac is reachable, so those paths remain unrun.
