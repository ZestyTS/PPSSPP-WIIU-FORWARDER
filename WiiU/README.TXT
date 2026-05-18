
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

The Wii U entrypoint checks for an optional autoboot config at:

sd:/wiiu/apps/ppsspp/autoboot.txt
sd:/ppsspp/autoboot.txt
installed title content/autoboot.txt

The config can contain either a plain path or key/value lines:

iso=sd:/ppsspp/games/My Game.iso
copy_to_sd=false

If no config is present, it tries to boot game.iso, game.cso, or game.pbp from
the installed title content folder. Direct installed-content boot is attempted
first. Set copy_to_sd=true only when testing a runtime that cannot read the
installed storage path directly.

Runtime diagnostics are written to:

sd:/ppsspp/autoboot.log

