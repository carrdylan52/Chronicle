#!/usr/bin/env bash
# The shared renderer's regression matrix for the linux-x64 build in port/build/pc.
#
#   scripts/port/matrix.sh            run it and compare with .kazarov/baseline/port/manifest.txt
#   scripts/port/matrix.sh --record   run it and make the results the baseline
#
# Runs land in .kazarov/matrix/run. Exits 1 when a run fails or a result differs from the manifest.
set -euo pipefail

record=0
case "${1:-}" in
    "") ;;
    --record) record=1 ;;
    *) echo "usage: $0 [--record]" >&2; exit 2 ;;
esac

cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
base=.kazarov/baseline/port
out=.kazarov/matrix/run
manifest=$base/manifest.txt
dungeon_frames="40 200 300 450 600"

rm -rf "$out"
mkdir -p "$out/save-interp"
echo '{"video":{"interpolation":false}}' > "$out/save-interp/config.json"

tools/kazarov/bin/kazarov shell env OUT="$out" DUNGEON_FRAMES="$dungeon_frames" sh -c '
    run() {
        name=$1 save=$2
        shift 2
        # A fresh save directory spends half a minute exporting the game text; data-3 pays it once.
        if [ "$save" != save-3 ] && [ ! -d "$OUT/$save/lang-export" ]; then
            mkdir -p "$OUT/$save"
            cp -r "$OUT/save-3/lang-export" "$OUT/$save/"
        fi
        status=0
        port/build/pc/darkcloud --headless --data "$PWD/data" --save "$PWD/$OUT/$save" "$@" > "$OUT/$name.log" 2>&1 || status=$?
        echo "$name $status" >> "$OUT/status.txt"
    }
    status=0
    (cd port/build/pc && env -u DC_DATA ctest --output-on-failure -j8) > "$OUT/ctest.log" 2>&1 || status=$?
    echo "ctest $status" >> "$OUT/status.txt"
    run data-3 save-3 --frames 3 --screenshot "$OUT/data-3.png"
    run data-120 save-cache --frames 120 --screenshot "$OUT/data-120.png"
    wc -c < "$OUT/save-cache/pipeline_cache.bin" > "$OUT/cache-bytes.txt" 2>&1 || true
    run data-120-warm save-cache --frames 120 --screenshot "$OUT/data-120-warm.png"
    run res-1280x720-120 save-1280 --frames 120 --width 1280 --height 720 --screenshot "$OUT/res-1280x720-120.png"
    run res-640x480-120 save-640 --frames 120 --width 640 --height 480 --screenshot "$OUT/res-640x480-120.png"
    run interp-off-120 save-interp --frames 120 --screenshot "$OUT/interp-off-120.png"
    for n in $DUNGEON_FRAMES; do
        run "dungeon-$n" "save-dungeon-$n" --jump dungeon:0 --fast-load --input port/src/tests/scripts/boot_dungeon.input \
            --frames "$n" --screenshot "$OUT/dungeon-$n.png"
    done
    DC_AUDIO_WAV="$OUT/title-full.wav" run title-audio save-audio --jump title --fast-load --frames 600
'

failed=0
while read -r name status; do
    if [ "$status" != 0 ]; then
        echo "FAIL $name exited $status (see $out/$name.log)"
        failed=1
    fi
done < "$out/status.txt"

python3 scripts/port/wav_summary.py "$out/title-full.wav" "$out/title-10s.wav" > "$out/title-10s.txt"

ctest_value() {
    local total failed skipped
    total=$(sed -n 's/.*tests failed out of \([0-9]*\)$/\1/p' "$out/ctest.log")
    failed=$(sed -n 's/.* \([0-9]*\) tests failed out of .*/\1/p' "$out/ctest.log")
    skipped=$(grep -c '(Skipped)$' "$out/ctest.log" || true)
    echo "total=$total failed=$failed skipped=$skipped"
}

# The binary reports the pipelines it compiles in RendererInit, which is the renderer's only
# vkCreateGraphicsPipelines call; a count of pipelines created after init does not exist yet.
pipelines_value() {
    local total ready
    total=$(sed -n 's|^compiling shaders \([0-9]*\)/\1$|\1|p' "$out/$1.log")
    ready=$(sed -n 's/^gfx: \([0-9]*\) pipelines ready in .*/\1/p' "$out/$1.log")
    echo "compiled=$total ready=$ready"
}

pngs="data-3 data-120 data-120-warm res-1280x720-120 res-640x480-120 interp-off-120"
for n in $dungeon_frames; do pngs="$pngs dungeon-$n"; done

current() {
    local file
    for name in $pngs; do
        file=$name.png
        echo "$name exact $(sha256sum "$out/$file" | cut -d' ' -f1) $file"
    done
    echo "title-10s exact $(sha256sum "$out/title-10s.wav" | cut -d' ' -f1) title-10s.wav"
    echo "title-10s-summary audio $(tr ' ' , < "$out/title-10s.txt") -"
    echo "ctest value $(ctest_value | tr ' ' ,) -"
    echo "pipelines-cold value $(pipelines_value data-120 | tr ' ' ,) -"
    echo "pipelines-warm value $(pipelines_value data-120-warm | tr ' ' ,) -"
    echo "pipeline-cache-bytes info $(cat "$out/cache-bytes.txt") -"
}
current > "$out/current.txt"

if [ "$record" = 1 ]; then
    [ "$failed" = 0 ] || { echo "not recording a failed run" >&2; exit 1; }
    mkdir -p "$base"
    for name in $pngs; do cp "$out/$name.png" "$base/"; done
    cp "$out/title-10s.wav" "$base/"
    {
        echo "# name kind value file (under $base); written by scripts/port/matrix.sh --record"
        echo "# build $(git rev-parse HEAD) $(date -u +%Y-%m-%dT%H:%MZ)"
        cat "$out/current.txt"
    } > "$manifest"
    echo "recorded $manifest"
    exit 0
fi

[ -f "$manifest" ] || { echo "no $manifest; run $0 --record first" >&2; exit 2; }

audio_close() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        split(a, x, /[,=]/); split(b, y, /[,=]/)
        # rms and peak level within 0.5 dB, loudest bin within 2 bins (5.86 Hz each)
        if ((x[2] - y[2])^2 > 0.25 || (x[4] - y[4])^2 > 0.25 || (x[6] - y[6])^2 > 11.72^2) exit 1
    }'
}

declare -A now
while read -r name _ value _; do now[$name]=$value; done < "$out/current.txt"
while read -r name kind value file; do
    case "$name" in "#"*|"") continue ;; esac
    got=${now[$name]:-missing}
    if [ "$kind" = info ]; then
        echo "info $name: $got (baseline $value)"
    elif [ "$got" = "$value" ]; then
        echo "ok   $name"
    elif [ "$kind" = audio ] && audio_close "$got" "$value"; then
        echo "ok   $name (within tolerance: $got vs $value)"
    else
        echo "FAIL $name: $got, baseline $value$([ "$file" != - ] && echo " ($base/$file vs $out/$file)")"
        failed=1
    fi
done < "$manifest"
exit "$failed"
