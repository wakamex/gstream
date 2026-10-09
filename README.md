# gstream

A small desktop app for watching Twitch. Sign in once, see which of the channels you follow are live, and watch one in the window. It is one executable for Linux and Windows, written in C on [gesso](https://github.com/wakamex/gesso), and embeds no browser.

gstream is not made or endorsed by Twitch.

## Size and memory

Measured on Windows 11 (Ryzen 7 3800XT, RTX 3080). CPU is the share of one core; memory is the private working set.

| Measure | gstream |
|---|---|
| Download | 2.5 MB zip, one 6.3 MB executable |
| Browsing the list, idle | 22 MB, 0.4% CPU |
| Playing a 1080p60 channel, Direct3D 11 decoding | 42 MB, 12% CPU |
| Playing 1080p60, software decoding | 91 MB, 36% CPU |

## Using it

Download the archive for your system from the Releases page, unpack it and run `gstream` (`gstream.exe` on Windows). Click "Sign in with Twitch", enter the code it shows on Twitch's page, and the list of live channels you follow appears. Click one to watch it.

Keys:

- `/` or `Ctrl+F` searches the list; arrow keys, `Page Up`, `Page Down`, `Home` and `End` move through it; `Enter` watches.
- `F` or the Full button shows the player alone, full screen; `Esc` goes back.
- `M` mutes. `F1` shows the diagnostics overlay: the rendition, buffer, decoding path, dropped frames, clock error, CPU and memory.
- `F11` or `Alt+Enter` makes the window full screen.

The quality is chosen automatically by default: gstream measures how fast segments arrive and moves between the channel's renditions as the connection allows, showing the one in use ("Auto 720p60"). The player bar's quality menu fixes a rendition instead, down to audio only. Video is decoded by the graphics card where the system offers it ([Video Acceleration API (VA-API)](https://github.com/intel/libva) on Linux, Direct3D 11 on Windows) and in software otherwise; `--software` forces software decoding.

`gstream --help` lists the command-line options, such as `--play CHANNEL` to start watching a channel straight away and `--sign-in` to sign in from a terminal.

The saved session is encrypted for your Windows user with the [Data Protection API (DPAPI)](https://learn.microsoft.com/en-us/windows/win32/seccng/cng-dpapi) on Windows, and is a file readable only by your user on Linux.

## Building

gstream builds with [Zig](https://ziglang.org/) 0.16.0, which fetches every dependency:

```sh
zig build --release=fast
zig build test
```

The build signs in as gstream's own Twitch application. To use your own registration, build with `-Dclient-id=YOUR_CLIENT_ID`.

## License

gstream is MIT licensed (`LICENSE`). The executable also contains [FFmpeg](https://ffmpeg.org/)'s libavcodec and libavutil under the GNU Lesser General Public License 2.1 or later, and gesso, [Simple DirectMedia Layer (SDL)](https://www.libsdl.org/), [stb](https://github.com/nothings/stb), [kb_text_shape](https://github.com/JimmyLefevre/kb) and [Opus](https://opus-codec.org/) under their own licenses; each release archive holds their texts in `licenses/`. Every release attaches FFmpeg's source and the files gesso builds it with.
