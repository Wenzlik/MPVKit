#!/bin/sh
# Post-build verification of this fork's FFmpeg (run after `make build`).
#   Sources/BuildScripts/verify/verify.sh [build area, default ./dist]
# Needs Homebrew ffmpeg (fixtures only), pkg-config and python3.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
DIST=$(cd "${1:-dist}" && pwd)
EXPECTED=${EXPECTED_VERSION:-n8.1.3-eac3-vt-sa.1}
ARCH=$(uname -m)
THIN="$DIST/FFmpeg/macos/thin/$ARCH"
XCF="$DIST/release/xcframework"
WORK=$(mktemp -d /tmp/mpvkit-verify.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

# 1. Every slice: configure result + symbols in the shipped binaries.
python3 -I "$HERE/check_config.py" "$DIST"

# 2. Fixtures: 10 s video + audio + ASS subtitle + one chapter, plus a 20 MB
#    or a 16 KiB attachment (the latter is below avio's 32 KiB short-seek
#    threshold) either before the first Cluster (ffmpeg -attach writes it in
#    the header) or after the Cues, reachable only via the SeekHead.
cd "$WORK"
head -c 20971520 /dev/urandom > big.ttf
head -c 16384 /dev/urandom > small.ttf
printf '[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize\nStyle: Default,Arial,20\n\n[Events]\nFormat: Layer, Start, End, Style, Text\nDialogue: 0,0:00:01.00,0:00:04.00,Default,hello\n' > sub.ass
printf ';FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=5000\ntitle=one\n' > chap.txt
FF="ffmpeg -v error -y -f lavfi -i testsrc=d=10:s=320x240:r=25 -f lavfi -i sine=d=10 -i sub.ass -f ffmetadata -i chap.txt -map 0 -map 1 -map 2 -map_chapters 3 -c:v mpeg4 -g 25 -c:a aac -c:s ass"
$FF base.mkv
for a in big small; do
    $FF -attach $a.ttf -metadata:s:t mimetype=application/x-truetype-font linear-$a.mkv
    python3 -I "$HERE/mkv_attach_at_end.py" base.mkv seekhead-$a.mkv $a.ttf
done

# 3. Build the check against the macOS xcframework slice; dependencies come
#    from the FFmpeg pkg-config files of the same build (static libraries).
#    Some deps (gmp) ship no .pc, so pkg-config emits a bare -lgmp: add -L for
#    every library directory of this build; MoltenVK/shaderc are C++ (-lc++).
FWS=""
LIBDIRS=""
for d in "$DIST"/*/macos/thin/"$ARCH"/lib; do LIBDIRS="$LIBDIRS -L$d"; done
for l in Libavformat Libavcodec Libavfilter Libswscale Libswresample Libavutil; do
    FWS="$FWS $(ls -d "$XCF/$l.xcframework"/macos-*/"$l.framework/$l")"
done
export PKG_CONFIG_PATH=$(ls -d "$DIST"/*/macos/thin/"$ARCH"/lib/pkgconfig | tr '\n' ':')
# shellcheck disable=SC2046,SC2086
clang -O1 -o verify "$HERE/verify.c" -I"$THIN/include" $FWS $LIBDIRS \
    $(pkg-config --static --libs libavformat libavfilter) -lc++ -Wl,-dead_strip

./verify "$EXPECTED" "$WORK"/linear-big.mkv "$WORK"/linear-small.mkv \
    "$WORK"/seekhead-big.mkv "$WORK"/seekhead-small.mkv
