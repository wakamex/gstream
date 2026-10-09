# Motivation: a native, cross-platform Twitch client

## Summary

gstream is a standalone desktop application for watching Twitch on Linux, Windows and macOS. It embeds no browser engine and does three things: signs in to Twitch, shows which followed channels are live, and plays a stream inside its own window. It owns only the part of Twitch's API it needs, and it is written in C on [gesso](https://github.com/wakamex/gesso), a small library on [Simple DirectMedia Layer (SDL) 3](https://www.libsdl.org/), so that it installs in a few megabytes and runs in tens of megabytes of memory.

## The problem

The ways to watch Twitch on the desktop today are all unsatisfying:

- The browser. twitch.tv runs a heavy, GPU-hungry single-page app on top of an HTML5 player. On weaker machines or while multitasking it stutters, spins up fans and drains batteries. Extensions for emotes or ad handling routinely break playback.
- The official desktop apps are gone. The old Curse-derived Twitch app was discontinued years ago, and Twitch Studio was retired in May 2024. There is no first-party desktop client for watching.
- Older third-party desktop clients are maintenance-bound. They show there is demand, but they tend to sit on aging web stacks, depend on one or two maintainers, or lack a real list of followed channels.
- Player mashups are clever but incomplete. A standalone player beside a chat or navigation app works for power users but is not a coherent watch client.

There is room for an actively maintained, native, browser-free desktop Twitch client.

## Design principles

### Native, cross-platform, one codebase

gstream runs as a real desktop application on Linux, Windows and macOS: low resource use, fast start-up, and no dependence on a browser's quirks. No platform is an afterthought; every module it uses works on all three.

### No embedded browser engine

gstream does not ship Chromium, Electron, a WebView2 or WKWebView surface, or any other browser engine. The reasons are the ones that make the browser experience bad:

- A bundled browser engine is tens to hundreds of megabytes, and a large runtime, for an app whose job is to show one video and a list.
- Running Twitch's web stack brings its resource use, its digital rights management fragility and its extension breakage along with it.
- A native interface driving a native player controls caching, latency and output quality directly.

### Small, measured, and owned

gstream draws its own interface with gesso and decodes video with a trimmed build of [FFmpeg](https://ffmpeg.org/)'s libavcodec, using each system's hardware decoder where there is one. Everything is compiled into one executable. Size and memory are measured on every milestone, with the targets in the plan.

### Own the playback resolver

Watching Twitch means resolving a playable stream: requesting a playback access token, building the Usher HLS request, choosing a rendition and classifying failures. gstream does this itself, in process, so there is no separate resolver to install. This makes gstream responsible for keeping up when Twitch changes its playback flow, in return for one app with nothing else to set up.

### Own only the Twitch API surface it needs

gstream uses the official Twitch Helix API for sign-in, followed live channels and channel metadata, and the public web playback flow to obtain stream URLs. Anything beyond watching stays out of the first release.

## Core capabilities

### Sign-in

OAuth against Twitch through the device-code flow, as a public desktop client: the user authorizes in their own browser, so gstream needs no embedded web view and ships no client secret. The access and refresh tokens are kept for the current user: encrypted with the Windows Data Protection API on Windows, and in a file only the user can read on Linux and macOS.

### Followed channels live now

The list prior tools lack: which followed channels are live right now, with title, category, viewer count, uptime and thumbnail. It comes from Helix's followed-streams endpoint, polled about every 90 seconds. EventSub over WebSocket is a possible later upgrade.

### Stream playback in the window

The chosen stream plays inside the window with gstream's own controls. gstream reads Twitch's HLS playlists and segments itself and decodes them with libavcodec, in hardware where the system allows. A stream is resolved only when the user picks a channel, while the cheap Helix poll keeps the list current.

## Non-goals

- Broadcasting or any streaming-out feature.
- A built-in chat client in the first release.
- An embedded browser engine, including for the watch surface.
- Push notifications through EventSub at first.
- A replacement for Twitch's directory, browsing or search beyond followed live channels.
