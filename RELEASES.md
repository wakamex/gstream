# Releases

streamit is released as prebuilt archives on GitHub Releases, built and published by `.github/workflows/publish.yml` from a `v*` tag. The version source is `.version` in `build.zig.zon`.

## Assets

| Asset | Zig target | File |
|---|---|---|
| Linux x86-64, glibc 2.27 or newer | `x86_64-linux-gnu.2.27` | `streamit-vX.Y.Z-x86_64-linux.tar.gz` |
| Windows x86-64 | `x86_64-windows-gnu` | `streamit-vX.Y.Z-x86_64-windows.zip` |
| FFmpeg's source and build files | none | `streamit-vX.Y.Z-ffmpeg-source.tar.gz` |

Both binaries are built on Linux with Zig 0.16.0. gesso is pinned by commit in `build.zig.zon`; to build against a local gesso checkout while changing both, use `zig build --fork=../gesso`. The Linux floor is glibc 2.27 (Ubuntu 18.04); gesso supplies the one newer glibc function SDL links. To check the floor, run the Linux archive in `docker.io/library/ubuntu:18.04` with `--demo --shot`. macOS is built and tested by `.github/workflows/macos.yml` but not released.

Each archive holds the executable, `README.md`, `LICENSE`, and a `licenses/` folder with the license texts of everything compiled in: gesso (MIT), stb, kb_text_shape (zlib), Opus (BSD-3-Clause), FFmpeg (GNU Lesser General Public License (LGPL) 2.1 or later), SDL3 with its REUSE license set, and on Linux SDL's Wayland protocol code and the VA-API headers. FFmpeg is linked statically, so every release also attaches FFmpeg's unmodified release tarball and the files gesso builds it with, which together let a user relink streamit with a modified FFmpeg. `release/stage.sh` builds one asset:

```sh
release/stage.sh linux dist
release/stage.sh windows dist
release/stage.sh sources dist
```

The Twitch application's Client ID is compiled in. `stage.sh` takes it from `TWITCH_CLIENT_ID` in the environment or from `.env`; `publish.yml` reads the repository variable `TWITCH_CLIENT_ID` and refuses to build without it.

## Checks

`validation.yml` runs the tests, builds all three assets, starts the Linux build and renders a frame headless (`--demo --shot`), then does the same with the Windows build on a Windows runner. Its gate job, `validate`, runs even when a build is skipped and passes only when both succeeded (`wakamex/release-actions/validate-gate`); it is the required check `release-eligible / validate`, produced on every push to `main` by `release-eligibility.yml`.

## Releasing vX.Y.Z

1. Set `.version` in `build.zig.zon` to `X.Y.Z` and write `release-notes/vX.Y.Z.md`.
2. Commit both as `Release vX.Y.Z` and push `main` without tags.
3. Wait for `release-eligible / validate` to pass on that commit, and check that remote `main` still points to it.
4. Tag it with an annotated `vX.Y.Z` and push only the tag.

`publish.yml` then reruns validation, checks that the tag is annotated, matches `build.zig.zon` and has release notes (`wakamex/release-actions/verify-release-tag`), and rebuilds the assets from the tag. The shared `wakamex/release-actions` binary release workflow writes `SHA256SUMS`, attests every asset and verifies the attestations, and creates the Release from the notes file. Rerunning it on a published tag verifies the existing Release instead of replacing it. A broken release is fixed by a new version; tags and published assets are never moved or replaced.

The attestations are signed by the shared workflow, so verify a downloaded asset with:

```sh
gh attestation verify ASSET --repo wakamex/streamit \
  --signer-workflow wakamex/release-actions/.github/workflows/binary-release.yml
gh release verify-asset vX.Y.Z ASSET --repo wakamex/streamit
```

## One-time setup

- Set the repository variable `TWITCH_CLIENT_ID`.
- Turn on immutable releases in the repository settings.
- Once `release-eligible / validate` has passed on `main`, add a tag ruleset named `Validated release tags` for `refs/tags/v*` that requires that check (from the GitHub Actions app that produced it), with no bypass actors, deletion restricted and non-fast-forward updates blocked.
