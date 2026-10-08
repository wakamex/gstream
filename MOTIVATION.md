# Motivation: A Native, Cross-Platform Twitch Client

## Summary

We want a standalone desktop application for watching Twitch that runs natively
on Linux, Windows, and macOS, embeds no browser engine, and provides three core
capabilities: account login, a live "who's online" view of followed channels,
and in-app stream playback. Playback resolution is native and in-process; video
is displayed through embedded libmpv. The application owns only the Twitch API
surface it actually needs.

The implementation direction, module boundaries, and milestone plan live in
[ARCHITECTURE.md](ARCHITECTURE.md).

## The Problem

The viable ways to watch Twitch on the desktop today are all unsatisfying:

- **The browser.** twitch.tv runs a heavy, GPU-hungry single-page app on top of
  an HTML5 player. On weaker machines or while multitasking it stutters, spins
  up fans, and drains battery. Third-party extensions for emotes/ad handling
  routinely break playback, and DRM/Widevine paths are fragile on Linux in
  particular.
- **The official desktop apps are gone.** The old Curse-derived Twitch desktop
  app was discontinued years ago, and Twitch Studio was retired in May 2024.
  There is no first-party desktop client for watching anymore.
- **Older third-party desktop clients are maintenance-bound.** The closest
  existing projects prove there is demand, but they tend to sit on aging web
  stacks, depend on one or two maintainers, or lack a real follow/discovery
  surface.
- **Player mashups are clever, not complete.** Combining a standalone player
  with a chat or navigation app can work for power users, but it does not create
  a coherent native watch client.

There is room for an actively maintained, native, browser-free desktop Twitch
client. That is the gap this project fills.

## Design Principles

### 1. Native, Cross-Platform, One Codebase

Run as a real desktop application on Linux, Windows, and macOS. Native means low
resource usage, fast startup, proper OS integration, and no dependence on a
system browser's quirks. Cross-platform from day one means no platform should be
a second-class afterthought.

### 2. No Embedded Browser Engine

This is the defining constraint. We will not ship Chromium, NW.js, Electron, a
WebView2/WKWebView surface, or any other browser engine as the application shell.

The reasons are the same ones that make the browser experience bad in the first
place:

- **Weight.** A bundled browser engine is tens to hundreds of MB and a large
  runtime footprint for an app whose job is "show one video and a list."
- **The website's problems become our problems.** Resource heaviness, DRM
  fragility, and extension-induced breakage all come from running Twitch's web
  stack. A browser shell re-imports exactly what we are trying to escape.
- **Control.** A native UI driving a native player gives us direct control over
  caching, latency, and output quality that a webview cannot expose.

### 3. Own The Playback Resolver

The moving part of watching Twitch is resolving a playable stream: requesting a
playback access token, building the Usher HLS request, selecting a playlist
variant, and classifying failures. We now own a small native implementation of
that path instead of requiring users to install a separate resolver.

This increases our responsibility when Twitch changes playback behavior, but it
also gives the app a cleaner product shape: one app, bundled playback engine, no
Python toolchain, no external executable setup, and no confusing diagnostics for
normal users.

### 4. Own Only The Minimum Twitch API Surface

We implement against the official Twitch Helix API for login, followed-live
metadata, and per-channel metadata. Playback resolution uses the public Twitch
web playback flow needed to obtain HLS URLs. Anything beyond the watch loop stays
out of v1.

## Core Capabilities

### Login

OAuth against Twitch, performed natively. The v1 path is Twitch's device-code
flow as a public desktop client: it uses the user's browser for authorization,
requires no embedded webview, and avoids shipping a client secret. We persist the
resulting access and refresh tokens in the best available platform store. On
Linux, that means Secret Service when a provider is usable, otherwise an
app-local profile file protected by owner-only permissions with explicit "not
encrypted" status text.

### Follows / Who's Online

The feature prior tools lack: a real discovery surface showing which followed
channels are live right now, with title, game/category, viewer count, uptime, and
thumbnail.

- Primary path: `GET /helix/streams/followed`.
- Liveness is poll-based on a 60-120 second interval.
- EventSub over WebSocket remains a possible post-v1 upgrade.

### Embedded Stream Playback

The selected stream plays inside the application window, with our own controls
around it. The native resolver obtains the HLS URL and embedded libmpv renders
it. Per-channel resolution happens only when the user actually clicks a channel
to watch, while the cheap Helix poll keeps the live grid current.

## Player Choice

libmpv is the production player backend. It gives us a clean render API, strong
HLS playback behavior, useful live-latency controls, broad FFmpeg-based codec
coverage, and a focused dependency surface.

## Non-Goals

- No broadcasting or streaming-out features.
- No built-in chat client for v1.
- No embedded browser engine, including for the watch surface.
- No push notifications via EventSub initially.
- No full Twitch directory/browse/search replacement beyond followed-live.

## Why Now

The hard parts are now tractable in a focused app. The Helix surface we need is
small and typed, the native playback resolver is bounded, and libmpv gives us a
real embedded player. The remaining work is a native cross-platform shell with
login, token storage, a polling live grid, and hardened packaging.
