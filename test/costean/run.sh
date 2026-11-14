#!/bin/bash
# Costean OS host unit tests: pool split + failover, scheduler, governor/auto-tune, long history,
# Bitaxe 602 profile values and the 128x32 OLED renderer.
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
for t in t_split t_sched t_tune t_602 t_oled; do $W/$t || rc=1; done
exit $rc
