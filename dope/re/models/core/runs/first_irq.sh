#!/bin/sh
# first runs of each part's int 8 handler: run each part's snapshot for 40 frames with --trace on the handler
cd "$(dirname "$0")/../../.."
run() { python3 run_dope.py --load snaps/part_$1.snap --out models/core/runs/irq_$1 --frames $2 --trace $3 > models/core/runs/irq_$1.log 2>&1; }
run p1 47 0F74:0020 &
run p2 2745 0F74:1A21 &
run p3 10063 0F74:1A43 &
run p4 13678 0F74:1420 &
run p5 21122 0F74:15D7 &
run p6 25628 0F74:17EE &
run p7 29637 0F74:1A47 &
wait
