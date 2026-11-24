#!/bin/bash
# Builds MyInput for macOS into build/mac:
#   myinput                  the launcher (see mac/inject)
#   libmyinput_hook.dylib    the hook it loads into the target app (see mac/hook)
#   Configs/                 the configs installed on first run
# Needs only the Xcode command line tools. Binaries are universal (Apple silicon + Intel).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/.."
out="$root/build/mac"
obj="$out/obj"
mkdir -p "$obj" "$out/Configs"

arch=(-arch arm64 -arch x86_64)
common=(-std=c++20 -O2 -fvisibility=hidden -mmacosx-version-min=12.0 "${arch[@]}")
warn=(-Wall -Wextra -Wno-unused-parameter)

# The engine is the Windows source, compiled as-is: mac/compat/ has symlinks to the shared headers
# plus stand-ins for the Windows-only ones. Its warnings are MSVC-isms, not ours to fix here.
clang++ "${common[@]}" -w -include "$here/compat/Prelude.h" -I"$here/compat" -I"$here/hook" \
    -c "$here/hook/Engine.cpp" -o "$obj/Engine.o"
clang++ "${common[@]}" "${warn[@]}" -fobjc-arc -I"$here/hook" -c "$here/hook/SdlPad.mm" -o "$obj/SdlPad.o"
clang++ "${common[@]}" "${warn[@]}" -fobjc-arc -I"$here/hook" -c "$here/hook/Hook.mm" -o "$obj/Hook.o"
clang++ "${common[@]}" -dynamiclib -o "$out/libmyinput_hook.dylib" \
    "$obj/Engine.o" "$obj/SdlPad.o" "$obj/Hook.o" -framework Cocoa -framework Carbon

clang++ "${common[@]}" "${warn[@]}" -fobjc-arc "$here/inject/Inject.mm" -o "$out/myinput" \
    -framework AppKit -framework Security

cp "$root/src/_default.ini" "$here/configs/"*.ini "$out/Configs/"

echo "Built $out/myinput"
