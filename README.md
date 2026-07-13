<div align="center">

# ToastyReplay

<img src="./logo.png" width="180" alt="ToastyReplay logo" />

Frame-accurate recording, playback, editing, conversion, and rendering for Geometry Dash.

[![Geode](https://img.shields.io/badge/Geode-v5.8.1-blue?style=flat-square)](https://geode-sdk.org)
[![Geometry Dash](https://img.shields.io/badge/Geometry%20Dash-2.2081-green?style=flat-square)](https://store.steampowered.com/app/322170/Geometry_Dash/)
[![Version](https://img.shields.io/badge/version-v2.2.1-orange?style=flat-square)](https://github.com/ToastexGD/ToastyReplay/releases)
[![Discord](https://img.shields.io/badge/Discord-Join-5865F2?style=flat-square&logo=discord&logoColor=white)](https://discord.gg/JWkVm7cUhH)

[Website](https://toastyreplay.xyz/) | [Buy Pro](https://toastyreplay.xyz/) | [Ko-fi](https://ko-fi.com/toastexgd) | [Report an issue](https://github.com/ToastexGD/ToastyReplay/issues)

</div>

## About

ToastyReplay is a replay bot for Geometry Dash. It records and plays Vanilla and CBS macros, supports classic and platformer levels, handles both players, and includes macro conversion, editing, click sounds, rendering, and practice tools.

Version 2.2.0 is a major rebuild. TTR3 is now the default recording format, the menu can use either ImGui or native Cocos2d controls, conversion is stricter, and the renderer is available as an optional beta.

## What is new in v2.2.0

- TTR3 recording with exact input timing, TPS events, anchors, checkpoints, persistence attempts, and replay metadata.
- A native Cocos2d menu with Native, Toasty, Ocean, Forest, and Violet themes.
- Replay search, pagination, native replay actions, and an updated macro editor.
- An optional beta renderer with presets, GPU encoder detection, audio controls, color correction, custom output settings, and resolutions up to 4K.
- Native Geode keybinds with migration from older ToastyReplay bindings.
- English, Spanish, French, Vietnamese, and Simplified Chinese interfaces.
- Editor playtest support for replays, trajectory, and hitboxes.
- A full-window Credits and Support page with team credits, special thanks, Buy Pro, Ko-fi links, and a moving orange Geometry Dash background.
- Major fixes for conversion timing, high-TPS rendering, frame advance, Safe Mode, and native menu updates.

See [CHANGELOG.md](./CHANGELOG.md) for the complete release notes.

## Replay features

- Vanilla and CBS recording and playback.
- TTR3 as the default native format.
- Legacy TTR, TTR2, and GDR loading and playback.
- Classic, platformer, two-player, practice mode, checkpoint, and start position support.
- Replay search, format and accuracy badges, pagination, duplicate, rename, delete, convert, and edit actions.
- A timeline editor for Vanilla macros with per-player lanes and undo and redo history.
- Deterministic RNG support for levels that use random triggers.

## Conversion

ToastyReplay can detect and convert supported foreign macros to TTR3 or GDR. Current native import support includes:

- MegaHack JSON and binary
- TasBot, zBot, YBot, YBot 2, XBot Frame, and xdBot
- Amethyst, Echo, GDMO, ReplayBot, Rush, KDBot, DDHOR, RBot, and Zephyrus
- ReplayEngine 1, 2, and 3
- Silicate 1, 2, and 3
- TCBot, GDR2, GDR JSON, UvBot, and plaintext input dumps
- Legacy TTR and TTR2 upgrades to TTR3

Conversion keeps the original file. If a source contains data ToastyReplay cannot preserve accurately, conversion is blocked with an explanation instead of silently producing a broken TTR3 macro.

## Rendering and click sounds

- Optional beta video output from 720p through 4K, plus custom resolutions.
- Render presets, configurable FPS, codec, bitrate, pixel format, and output location.
- FFmpeg API support with an executable fallback.
- Music, game audio, click sounds, volume controls, and audio fades.
- Custom click packs with separate Player 1 and Player 2 selections.
- Optional endscreen, new best, and effect controls.

The FFmpeg API dependency is optional. Install it through Geode or configure a local FFmpeg executable if you plan to render videos.

## Gameplay tools

- Per-player noclip with accuracy tracking and configurable death feedback.
- Hitboxes, trajectory, Layout Mode, Disable Shaders, No Mirror, and Safe Mode.
- Speed control with audio pitch sync and frame advance.
- Autoclicker and custom click sounds.
- Configurable visual suppression, respawn timing, and replay labels.

## Free and Pro

The Free edition includes the complete Vanilla and CBS replay workflow, macro conversion, editing, gameplay tools, click sounds, and the beta renderer.

[ToastyReplay Pro](https://toastyreplay.xyz/) adds CBF recording and playback, 8K rendering, the full Pro automation set, extra two-player tools, additional render controls, and support for Windows, macOS, and Android.

## Platforms

| Platform | Geometry Dash | Status |
| --- | --- | --- |
| Windows | 2.2081 | Supported |
| macOS | 2.2081 | Supported |

Geode 5.8.1 or newer is required.

## Installation

1. Install [Geode](https://geode-sdk.org).
2. Install ToastyReplay through Geode or place the `.geode` package in the Geometry Dash `geode/mods` folder.
3. Launch Geometry Dash and open the menu with the configured ToastyReplay keybind.

Free and Pro cannot be enabled at the same time. Remove or disable one edition before enabling the other.

## Team

- ToastexGD, Main Developer and Founder
- human0443, Developer
- misko.bin, Developer
- __hopeandmiracle, Developer
- jimmybutlerfan, Playtester and Marketing
- Therealkeanan00, Playtester and Marketing

Special thanks to peony, Lead Developer of Silicate Bot; chagh.dev, Lead Developer of TCBot; GWDdoS, Owner of AstralBot; and kepe__, Owner of Ybot.

## Support

If something breaks, report it through the in-game issue form, open a [GitHub issue](https://github.com/ToastexGD/ToastyReplay/issues), or join the [Discord server](https://discord.gg/JWkVm7cUhH).

If you want to support continued development, visit [Ko-fi](https://ko-fi.com/toastexgd).
