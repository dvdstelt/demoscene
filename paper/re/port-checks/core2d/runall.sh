#!/bin/bash
cd /tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/core2d
export PYTHONPATH=/home/dennis/Downloads/acme_303/paper/re
export PYTHONDONTWRITEBYTECODE=1
SN=../snaps
python3 run_fade.py --out fade/c0 --ticks 121 > fade/c0.log 2>&1 &
prev=120
for s in 1150 1250 2000 3300 4500 5500 5800 6100 7000 8000 8700; do
  python3 run_fade.py --load --snap $SN/t$prev.snap --out fade/c$prev --ticks $((s+1)) > fade/c$prev.log 2>&1 &
  prev=$s
done
python3 run_fade.py --load --snap ../planes8700.snap --plane-budget 750000 --out fade/p8700 --ticks 12240 > fade/p8700.log 2>&1 &
wait
echo all done
