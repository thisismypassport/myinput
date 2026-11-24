#!/bin/bash
# Builds the probe against Homebrew's SDL2 and SDL3 and runs it with the hook injected.
# Needs: brew install sdl2 sdl3 (sdl2-compat works too). Run mac/build.sh first.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../../build/mac"
hook="$out/libmyinput_hook.dylib"
[ -f "$hook" ] || { echo "Build the hook first: mac/build.sh" >&2; exit 2; }

prefix="$(brew --prefix)"
flags=(-std=c++20 -fobjc-arc -O1 -I"$prefix/include" -L"$prefix/lib" -framework Cocoa -framework Carbon)

mkdir -p "$out/test/Configs"
cp "$here/probe.ini" "$out/test/Configs/probe.ini"

status=0
for sdl in 2 3; do
    probe="$out/test/probe_sdl$sdl"
    if [ "$sdl" = 3 ]; then
        clang++ "${flags[@]}" -DPROBE_SDL3 "$here/probe.mm" -lSDL3 -o "$probe"
    else
        clang++ "${flags[@]}" "$here/probe.mm" -lSDL2 -o "$probe"
    fi

    echo "== SDL$sdl =="
    DYLD_INSERT_LIBRARIES="$hook" \
        MYINPUT_HOOK_CONFIG_DIR="$out/test/Configs" \
        MYINPUT_HOOK_CONFIG=probe \
        "$probe" || status=1
done

exit $status
