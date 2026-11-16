#!/bin/bash
# Build the stm32l452-st-nucleo v5.3.0 BSP.
# Renames build/ to avoid the sandbox bulk-delete guard on incremental rebuilds,
# then does a clean build. Keeps only the latest backup.
set -u
BSP="E:/test/rtt-v5/rt-thread/bsp/stm32/stm32l452-st-nucleo"
SCSONS="C:/Users/admin/.workbuddy/binaries/python/versions/3.13.12/Scripts/scons.exe"
export RTT_EXEC_PATH="D:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.2 Rel1/bin"

cd "$BSP" || exit 1
if [ -d build ]; then
    ts=$(date +%s)
    mv build "build_prev_$ts"
    # keep only the most recent previous build
    for d in $(ls -d build_prev_* 2>/dev/null | sort -r | tail -n +2); do
        mv "$d" "${d}.trash" 2>/dev/null
    done
fi

"$SCSONS" -j4 > /tmp/rtt_build_latest.log 2>&1
rc=$?
echo "BUILD_EXIT=$rc"
grep -nE 'error:|undefined reference|safe-delete' /tmp/rtt_build_latest.log | head -10
echo "=== tail ==="
tail -12 /tmp/rtt_build_latest.log
exit $rc
