# gstream

Repository-specific decisions; packaging and release rules follow `/code/pacman/RELEASE_POLICY.md`, with build details in `RELEASES.md` and design decisions in `decisions.md`.

## Twitch client IDs

gstream uses two Twitch client IDs, and both are approved:

- gstream's own registered application ID, the default in `build.zig` (`-Dclient-id` overrides it), for device code sign-in and the Helix API (followed channels).
- Twitch's web-player ID, `WEB_CLIENT_ID` in `src/twitch.c`, for the GraphQL playback access token only. Twitch issues that token to its own player's ID and refuses others, so every browser-free Twitch player (streamlink, yt-dlp) sends it. If Twitch blocks it, playback fails with `TW_UNSUPPORTED` while sign-in and the channel list keep working.

Both IDs are public, so committing them is fine. Never commit a client secret; device code sign-in does not use one.
