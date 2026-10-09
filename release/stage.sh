#!/usr/bin/env bash
# Builds one release asset.
#   release/stage.sh linux|windows|sources OUT_DIR
# linux and windows build gstream for that target and pack it with the license texts of everything
# compiled into it: OUT_DIR/gstream-vVERSION-x86_64-linux.tar.gz or -x86_64-windows.zip. sources
# packs what the LGPL asks to accompany the binaries: FFmpeg's release tarball and the files gesso
# builds it with, as OUT_DIR/gstream-vVERSION-ffmpeg-source.tar.gz. VERSION is build.zig.zon's
# .version. Run from a clean checkout; zig 0.16 fetches the dependencies into zig-pkg/.
set -euo pipefail
cd "$(dirname "$0")/.."

target=${1:?linux, windows or sources}
out=$(mkdir -p "${2:?output directory}" && cd "$2" && pwd)
version=$(sed -n 's/^ *\.version = "\(.*\)",$/\1/p' build.zig.zon)
test -n "$version"
ffmpeg_version=9.0.2
ffmpeg_sha256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e

# A dependency's folder: its .path, relative to the build.zig.zon naming it, or its package folder
# from the .hash.
package() {
    local block dir hash
    block=$(grep -A3 "^ *\.$2 = \.{" "$1")
    dir=$(echo "$block" | sed -n 's/.*\.path = "\(.*\)".*/\1/p')
    if [ -n "$dir" ]; then (cd "$(dirname "$1")/$dir" && pwd); return; fi
    hash=$(echo "$block" | sed -n 's/^ *\.hash = "\(.*\)",$/\1/p')
    test -n "$hash" && test -d "zig-pkg/$hash" || { echo "no package for $2 in $1" >&2; exit 1; }
    echo "zig-pkg/$hash"
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
zig build --fetch=all  # (puts the dependencies, gesso's lazy FFmpeg among them, in zig-pkg/ for the lookups below)
gesso=$(package build.zig.zon gesso)

if [ "$target" = sources ]; then
    name=gstream-v$version-ffmpeg-source
    stage=$work/$name
    mkdir -p "$stage/gesso"
    curl -sSfL -o "$stage/ffmpeg-$ffmpeg_version.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$ffmpeg_version.tar.xz"
    echo "$ffmpeg_sha256  $stage/ffmpeg-$ffmpeg_version.tar.xz" | sha256sum -c --quiet
    cp -r "$gesso/ffmpeg" "$gesso/build.zig" "$gesso/build.zig.zon" "$gesso/LICENSE" "$stage/gesso/"
    cat > "$stage/README.txt" <<EOF
gstream $version links FFmpeg $ffmpeg_version's libavcodec and libavutil statically, under the
GNU Lesser General Public License version 2.1 or later (COPYING.LGPLv2.1 in the tarball).

ffmpeg-$ffmpeg_version.tar.xz is FFmpeg's unmodified release tarball.
gesso/ holds the files gesso builds it with: build.zig (see addFfmpeg) compiles the sources listed
in gesso/ffmpeg/<target>/sources.txt with the configure output beside them, which
gesso/ffmpeg/tools/import.sh generated.

To relink gstream with a modified FFmpeg, check out gstream and gesso at this release, change the
FFmpeg dependency in gesso's build.zig.zon to your copy, and run zig build.
EOF
    tar -czf "$out/$name.tar.gz" -C "$work" "$name"
    echo "$out/$name.tar.gz"
    exit
fi

case $target in
    linux) zig_target=x86_64-linux-gnu.2.27 exe=gstream ext=tar.gz ;;
    windows) zig_target=x86_64-windows-gnu exe=gstream.exe ext=zip ;;
    *) echo "unknown target: $target" >&2; exit 1 ;;
esac
name=gstream-v$version-x86_64-$target
zig build --release=fast -Dtarget="$zig_target" -p "$work/prefix"

stage=$work/$name
mkdir -p "$stage/licenses"
cp "$work/prefix/bin/$exe" README.md LICENSE "$stage/"
sdl=$(package "$gesso/build.zig.zon" sdl)
opus=$(package "$gesso/build.zig.zon" opus)
ffmpeg=$(package "$gesso/build.zig.zon" ffmpeg)
cp "$gesso/LICENSE" "$stage/licenses/gesso-LICENSE"
cp "$gesso/vendor/stb/LICENSE" "$stage/licenses/stb-LICENSE"
cp "$gesso/vendor/kb/LICENSE" "$stage/licenses/kb_text_shape-LICENSE"
cp "$opus/COPYING" "$stage/licenses/opus-COPYING"
cp "$ffmpeg/COPYING.LGPLv2.1" "$stage/licenses/FFmpeg-COPYING.LGPLv2.1"
mkdir "$stage/licenses/SDL"
cp -r "$sdl/LICENSE.txt" "$sdl/REUSE.toml" "$sdl/LICENSES" "$stage/licenses/SDL/"
if [ "$target" = linux ]; then  # SDL's Wayland protocol code and the VA-API headers, on Linux only
    linux_deps=$(package "$sdl/build.zig.zon" sdl_linux_deps)
    mkdir "$stage/licenses/SDL_linux_deps"
    cp -r "$linux_deps/LICENSE.txt" "$linux_deps/REUSE.toml" "$linux_deps/LICENSES" "$stage/licenses/SDL_linux_deps/"
    cp "$gesso/vendor/va/LICENSE" "$stage/licenses/libva-LICENSE"
fi

cd "$work"
rm -f "$out/$name.$ext"
if [ "$ext" = zip ]; then zip -qrX "$out/$name.$ext" "$name"; else tar -czf "$out/$name.$ext" "$name"; fi
echo "$out/$name.$ext"
