#!/bin/bash
# Costean OS host unit tests: pool split + failover, scheduler, governor/auto-tune, long history,
# Bitaxe 602 profile values, the 128x32 OLED renderer, the boot-loop guard and Wi-Fi failure reasons.
# usage: test/costean/run.sh   (from the repository root). Needs g++/gcc with AddressSanitizer.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd); W=${TMPDIR:-/tmp}/costean-unit; mkdir -p $W
F="-g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
S=$ROOT/main/costean; T=$ROOT/test/costean; rc=0
g++ -std=c++17 $F $T/test_pool_split.cpp $S/poolsplit.cpp -o $W/t_split
g++ -std=c++17 $F $T/test_scheduler.cpp $S/scheduler.cpp $S/tuning.cpp -o $W/t_sched
g++ -std=c++17 $F $T/test_tuning.cpp $S/tuning.cpp $S/rollup.cpp -o $W/t_tune
g++ -std=c++17 $F $T/test_board602.cpp $S/board602.cpp $S/tuning.cpp -o $W/t_602
gcc -std=c11 $F -I$S $T/test_lode_oled.c $S/lode_oled.c $S/lode_font.c -o $W/t_oled
gcc -std=c11 $F -I$S $T/test_ota_check.c $S/ota_check.c -o $W/t_ota
gcc -std=c11 $F -I$S -I$ROOT/components/connect/include $T/test_boot_guard.c $S/boot_guard_logic.c -o $W/t_guard
for t in t_split t_sched t_tune t_602 t_oled t_guard; do $W/$t || rc=1; done
# OTA upload checks, also against the last build outputs when they exist
B=$ROOT/build; FAC=$(ls $ROOT/../out-c*/esp-miner-factory-602-*.bin 2>/dev/null | tail -n 1)
$W/t_ota $B/esp-miner.bin $B/www.bin "${FAC:-/nonexistent}" || rc=1
python3 $T/check_oled_polarity.py || rc=1
python3 $T/check_psram_flash.py || rc=1
exit $rc
