# Changelog

## v2.2.0

### What's New?

- Added TTR3 as the default recording format with exact input timing, TPS events, anchors, checkpoints, persistence attempts, and replay metadata.
- Added replay search, pagination, format badges, accuracy badges, and native replay actions.
- Added native duplicate, rename, delete, convert, and edit flows.
- Updated the macro editor with per-player lanes and improved undo and redo handling for Vanilla macros.
- Added full classic, platformer, two-player, practice mode, checkpoint, and start position support.
- Added editor playtest support for replay playback, trajectory, and hitboxes.
- Added native Geode keybinds and migration from older ToastyReplay bindings.
- Kept legacy TTR, TTR2, and GDR loading and playback.
- Added TTR3 output for supported foreign macro formats and legacy ToastyReplay files.
- Added compatibility checks that block conversion when source data cannot be preserved accurately.
- Fixed GDR2 input decoding, extension validation, platformer controls, and physics anchor handling.
- Fixed TCBot v2 TPS and delta-time metadata, restart attempts, dynamic TPS changes, swift inputs, and deterministic RNG seeds.
- Fixed integer-frame inputs occasionally landing one tick early at TPS values such as 360, 1000, and 2026.
- Fixed dropped, duplicated, or reordered same-frame inputs.
- Kept single same-frame press and release taps in Vanilla mode while using CBS timing for repeated same-frame presses.
- Added an optional native Cocos2d menu alongside the classic ImGui menu.
- Added Native, Toasty, Ocean, Forest, and Violet themes to the Cocos2d menu.
- Added English, Spanish, French, Vietnamese, and Simplified Chinese interfaces with bundled CJK font support.
- Fixed language changes not refreshing the open Cocos2d menu.
- Updated the project to Geode 5.8.1 and Geometry Dash 2.2081.
- Replaced the old renderer with a new system that supports presets, GPU encoder detection, expanded codecs, audio controls, color correction, and custom output settings.
- Added per-player noclip, configurable accuracy decimals, noclip death hitboxes, respawn delay controls, Layout Mode colors, and expanded trajectory length controls.
- Added No Death Effect, No Effects, Hide Endscreen, and Hide New Best tools.
- Added a full-window Credits and Support page with a moving orange Geometry Dash background, a mobile-only close button, and Escape navigation on desktop.

### What's Fixed?

- Fixed high-TPS render scheduling, physics stepping, and macOS anchor synchronization.
- Fixed frame advance timing, music synchronization, rapid click audio, and render orientation.
- Fixed Safe Mode, respawn behavior, native popup lifetime, and menu refresh issues.
- Fixed click sound buffers cutting off longer release samples.
- Fixed several conversion failures that previously imported a file but could not produce an accurate TTR3 macro.

## v2.1.1

### What's fixed?

- Replaced the runtime-loaded watermark DLL with a private static watermark library for Index builds.
- Removed the bundled watermark DLL package path from the public Free build.

## v2.1.0

### What's New?

- Auto Safe Mode
- Pride Menu Icon change! (Optional)


## What's fixed?

- Fixed the 1 step delay to frame stepping when clicking on a frame.
- Dash orb glitch is fixed when not interacting with dash orbs correctly. 
- Fixed trajectory destroying destroyable items/blocks, breaking immersion. 
- Fixed fatal crash bug when opening editor with speedhack set to 0.627% (yes we don't know why this was happening)
- Fixed Label size calculations
- Fixed wave trail bug (use wave trail fix mod)

## v2.0.2

### What's fixed?

- conversion: fix Silicate accuracy + add Echo support; release v2.0.2
- Fix stuck-button bug: same-frame press+release (e.g. Silicate 'swift'
  clicks) were reordered release-first then deduped into a permanently held
  button. Affected every converted format with instant taps.
- parseSilicate3: accept Bugpoint special sections instead of throwing.
- Add Echo (.echo) import: binary (META), old JSON ('Echo Replay'), and new
  JSON ('inputs'); wired into format detection and the supported list.
- updated the version to v2.0.2

## v2.0.1

### What's new?

- Resizing menu for Android Devices
- Touch screen scrolling + Performance Boost

### What's fixed?

- Conversion for silicate macros now parse correctly (old header broke all inputs in the stack)
- Macro DB now accurately collects ttr2 macros and sends conversion data (forgot to accept the backend PR when I was setting it up 😭)
- Fixed some updater issues with the Pro Menu.
- Fixed touchscreen issues with Android 64bit devices.

## v2.0.0

### What's new?

- **Macro Conversion** Drop a macro from basically any other replay bot into your `replays/` folder and convert it to TTR2 or GDR. Supports MegaHack (JSON + Binary), TasBot, zBot, OmegaBot, YBot / YBot 2, xdBot, XBot Frame, Echo, Amethyst, Osu Replay, GDMO, ReplayBot, Rush, KDBot, DDHOR, QBot, RBot, Zephyrus, ReplayEngine 1 / 2 / 3, Silicate 1 / 2 / 3, GDR2, UvBot, TCBot, and plaintext dumps. Runs on a background thread so the menu doesn't freeze on huge macros, and tags the source row as "Converted" once its done.
- **TTR2 format** Better TTR2 with platformer mode flag, two-player flag, accuracy mode flags, RNG-locked flag, and extra metadata baked into the header. Same `.ttr` extension, same zlib payload, just way more info per file. Legacy TTR files are auto-detected and still load with no extra steps.
- **Platformer macro support** record, playback, and edit platformer levels (left / right inputs included, no longer useless lol).
- **Disable Shaders** new module that kills every shader layer the level applies. Practice in an unshaded view without having to edit the level.
- **Star ambient background** drifting twinkle particles in the menu. I cooked trust
- **UI Language setting** pick from the mod settings. English and Spanish are bundled, with more coming as translations roll in.
- **Discord device-code login** pair the 6-character code in your browser, done. No more dealing with the old 32-char hex session string.
- **Refresh-token auth** for the online client. Stays signed in across game restarts, and the token rotates on every refresh so a stolen token can't be replayed.
- **Frame editor commit model** proper commit-based history so undo/redo never gets out of sync with the file on disk. Ported from the Pro build.
- **Trajectory rewrite** pulled into its own module with a dedicated physics simulator. Portals, dash rings, pads, and trajectories line up with the actual gameplay closer than before.
- Geode SDK bumped from v5.3.0 to **v5.7.1**.

### What's fixed?

- HOLY SHIT I FIXED TRAJECTORY (the orb / pad / portal drift everyone complained about, FIXED)
- Fixed MP4 audio not playing on some mobile devices
- Stabilized the render audio muxing so exported videos don't desync near the end
- Fixed level names not actually updating when you load a different macro (old ass bug, finally caught it)
- Fixed the click sound buffer issue where packs with long release samples could cut off mid-press if you pressed again before the previous release finished playing
- Fixed a bunch of dev-branch crashes during the trajectory rewrite (frame stepper, checkpoint restore, shit like that)
- Online client no longer hits a port 3000 (Yes it was open) so now it goes through the proper domain over Toastyreplay
- Many more fixes.

## v1.3.0

- New recording format (TTR)
- Startposition support.
- CBS and CBF mode rework.
- New frame editor (for Vanilla macros)
- Video Rendering up to 4k with FFmpeg API
- Click Sounds and clickpack support with many customization options
- Complete Trajectory Mode rework with orbs and much more.
- Autoclicker up to max cps at TPS limits
- New online features including linking discord, issue reporting, and macro submissions.
- All new menu with themes, reworked background, and much more.

### What's fixed?

- Fixed the game disapearing at high tps
- TPS is now stable up to 1 million tps
- Fixed the editor crash when dying
- Fixed high tps crashing in editor
- Fixed FFmpeg breaking pulsing objects
- Fixed recording being unstable when using megahack
- Fixed menu animation sliding the menu down
- Fixed menu causing extreme lag when opening
- Fixed Practice Mode from causing extreme lag when respawning.
- Many more fixes.

## v1.2.1

- Fixed crashes.
- Added Geode v5 support.

## v1.2.0

- Added CBF compatibility with recording and playback sub-tick precision.
- Added Noclip death flash (customizable colors)
- Added background menu blur.

- Fixed Noclip slope physics being broken due to an internal property issue.
- Fixed Trajectory from corrupting player2 physics.
- Fixed Trajectory for player2, allowing it to be different for both players.
- Fixed Noclip accuracy deaths from going into negatives.
- Fixed menu not closing when playing back menu.
- Fixed versioning being completely broken.
- ported to v5.0.0-beta.3 of Geode

## v1.1.1

- Fixed any other file crashing the replay when loaded.
- Fixed Trajectory not showing orb inputs.
- Fixed the Noclip Accuracy going into the negatives (added custom accuracy changing)
- Fixed Safe Mode not activating when toggled.
- Fixed RNG from crashing the game when inputting a number other than 1.

## v1.1.0

- All new hitbox system, with On Death and Player Trail toggles.
- An all new UI with modular systems.
- Frame Replacement and Input Adjustments reworks, allowing for 100% accurate playback.
- New customization options, including colors, animations, and menu settings.
- All new keybind system which allows you to set a keybind for all modules.
- Important seed changes.
- Improved overall configuration to increase performance.

## v1.0.0

- Added frame advancing features (with physics distribution)
- Edited a lot of the codebase for copyright and ethical purposes
- CBF menu is removed
