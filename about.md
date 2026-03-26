### Geometry Dash's most accurate replay bot.
Record, playback, edit, and render frame-perfect macros with 100% accuracy.

Features
--
**Recording & Playback**
* Frame-perfect macro recording at any TPS
* Two formats: **TTR** (compact binary) and **GDR** (legacy)
* Full two-player and platformer support
* Practice mode recording with checkpoint snapshots
* Start position support with tick offset

**Accuracy Modes**
* **Vanilla** - standard frame-by-frame
* **CBS** (Click Between Steps) - native sub-step timing
* **CBF** (Click Between Frames) - microsecond precision for syzzi's mod
* Playback anchors that correct physics drift every second

**Frame Editor**
* Visual timeline with drag-to-edit input segments
* Per-player input lanes, zoom/scroll, overview bar
* Full undo/redo stack
* Drag edges to resize holds, move segments to retime inputs

**Video Rendering**
* FFmpeg export (720p to 4K, any FPS)
* Configurable codec, bitrate, and pixel format
* Mix in game audio, click sounds, or both
* Hide end screen / level complete popup

**Click Sounds**
* Custom click packs with hard/soft clicks and releases
* Separate packs for Player 1 and Player 2
* Softness slider, click delay randomization, background noise
* Auto-mixed into video renders

**Hacks & Tools**
* **Noclip** with accuracy %, collision limits, and death flash
* **Hitboxes** (always-on, on-death, or trail mode)
* **Trajectory** preview (312 frames ahead)
* **Safe Mode**, **Layout Mode**, **No Mirror**
* **Autoclicker** with per-player config
* **RNG Lock** for deterministic random triggers
* **Speed Control** with audio pitch sync
* **Frame Advance** (tick-by-tick stepping)

**Online**
* Discord login with macro uploading
* Bug report submission from in-game

**Customization**
* 5+ built-in themes with full custom colors
* Rebindable hotkeys for every feature
* Glow cycle animation, text scaling
* Animated menu transitions with easing

Credits
--
- [Figment](https://github.com/FigmentBoy) for permission to use some of [zBot's](https://github.com/FigmentBoy/zBot) features like trajectory and replay features.
- [Zilko](https://github.com/Zilko) for inspiring me (from [xdBot](https://github.com/Zilko/xdBot))
- [Jarvisdevil](https://github.com/thejarvisdevil) for helping me put my braincells together.
- [NinXout](https://github.com/ninXout) for inspiration similar to Eclipse Menu. (Received help for my hitbox implementation)
- [C++ and C++ Together Discords](https://discord.gg/WeBHv6b4WS) for helping me learn C++ and their amazing guides.
- [GDH by Toby](https://github.com/TobyAdd/GDH/blob/main/LICENSE) for being an amazing open source reference to fix up trajectory, hitboxes, and other bugs.
- And of course, [Geode](https://github.com/geode-sdk) for the amazing framework this is built on.

Updates are added monthly. If you find bugs, report them on Discord or open an issue.

https://discord.gg/JWkVm7cUhH
https://github.com/ToastexGD/ToastyReplay/issues

Thanks everyone! <3
