
### Build Instructions
tested with devkitPPC r38

make sure you have the latest ppsspp-ffmpeg submodule:
cd ffmpeg; git pull origin master

then create a build directory, then navigate to it and run:
cmake -DWIIU=ON path/to/ppsspp
make

if you need console output over network, set the PC_DEVELOPMENT_IP_ADDRESS define in ext/wiiu/logger.c and you can use the ext/wiiu/net_listen.sh script to listen to the log.
the run.sh script can be used to both send the compiled rpx to the HBL and listen to console output.

### Autoboot behavior

The Wii U entrypoint checks for an optional autoboot config in this order:

installed title content/autoboot.txt
sd:/wiiu/apps/ppsspp/autoboot.txt
sd:/ppsspp/autoboot.txt

The config can contain either a plain path or key/value lines:

iso=sd:/ppsspp/games/My Game.iso
copy_to_sd=false
performance_profile=fast

If no config is present, it tries to boot game.iso, game.cso, or game.pbp from
the installed title content folder. Direct installed-content boot is attempted
first. Set copy_to_sd=true only when testing a runtime that cannot read the
installed storage path directly.

Standalone installable titles skip the legacy IOSUHAX/MCP storage hook during
startup and mount a fs: device backed by Wii U FS paths. This lets packaged
paths such as fs:/vol/content/game.iso and fs:/vol/save/common resolve before
the PPSSPP frontend starts.

Packaged installable config is intentionally checked before SD config so a
stale sd:/ppsspp/autoboot.txt cannot redirect an installed WUP title to a
different game. Empty or comment-only config files are ignored and fallback
detection continues.

The optional performance_profile value accepts:

compatibility
balanced
fast
max_speed
custom

fast is the Wii U baseline used by UInjectForge. It keeps 1x rendering and
enables lighter graphics settings plus auto frameskip. balanced keeps safer
speed settings without forced frameskip. compatibility avoids the most aggressive
speed hacks. max_speed disables more visual work and sound for difficult games;
use it only for performance testing.

When performance_profile=custom, the runtime still starts from the fast
baseline unless individual keys override it. Supported custom keys are:

internal_resolution=1
frame_skip=1
auto_frame_skip=true
enable_sound=true
block_transfer_gpu=true
disable_slow_framebuf_effects=true
spline_bezier_quality=0

The same keys can also override the named profiles for A/B testing. Values are
clamped to the runtime's safe ranges: internal_resolution 1-4, frame_skip 0-5,
and spline_bezier_quality 0-2.

Runtime assets are loaded from installed title content/assets when present
(`ppge_atlas.zim`, `ui_atlas.zim`, `langregion.ini`, or flash0 font assets
are treated as asset-root markers), with sd:/ppsspp/assets as a fallback.
Saves and cache files use the installed title save root when available, with
sd:/ppsspp as a fallback. Installed title content is not treated as a writable
user-data location.

Runtime diagnostics are written to:

sd:/uinjectforge/ppsspp/autoboot.log

If SD logging cannot be opened, the runtime falls back to autoboot.log under
the selected writable root.

