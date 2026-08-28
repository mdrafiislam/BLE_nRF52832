# GitHub Actions

Firmware is built and published only when you ask for it. Every build becomes a GitHub Release.

## Cutting a release

**From a pull request** — put a marker anywhere in the PR title, then merge to `main`:

| Marker | v0.0.2 becomes |
|---|---|
| `[release]` or `[release:patch]` | `v0.0.3` |
| `[release:minor]` | `v0.1.0` |
| `[release:major]` | `v1.0.0` |

Example: `Fix SPI timing on wake [release:patch]`

**By hand** — Actions → *Firmware release* → *Run workflow*, pick a branch and a bump size.
Running from `main` publishes a normal release. Running from any other branch publishes a
**pre-release** instead, tagged `v0.0.3-my-branch.3500988`, so test builds never take a real
version number or clutter the release list.

A merge without a marker builds nothing and publishes nothing. Pull requests do not build.

## What you get

A release tagged `vX.Y.Z` with two attachments:

```
aim-bt-v0.0.3-pr42-1e82e15-debug.zip
aim-bt-v0.0.3-pr42-1e82e15-release.zip
```

`debug` is unoptimized, `release` is optimized for size. Each zip holds:

| File | Use it for |
|---|---|
| `merged-flash.hex` | First flash over SWD (app + SoftDevice + bootloader) |
| `firmware-dfu.zip` | Wireless DFU updates |
| `application.hex` | The app alone |
| `bootloader-settings.hex` | Bootloader settings |
| `manifest.json` | Which commit and tool versions produced this |
| `SHA256SUMS` | `sha256sum -c SHA256SUMS` to verify the others |

The tag is created for you. Never push a `vX.Y.Z` tag by hand.

## When it fails

Read the red step's error message — each one says what to do. The usual causes:

- The PR title has `[release` but not a marker that parses, or has two markers.
- The commit was already released. Release from a newer commit.
- The version already exists, because something else released first.

Fix the title or pick a newer commit, then merge or run again.

## One-time setup

CI downloads its toolchain from segger.com and Nordic. To stop depending on them, upload the two
pinned archives named in `tools.json` to a release tagged `tools`. CI uses them if present and
falls back to the vendors if not.
