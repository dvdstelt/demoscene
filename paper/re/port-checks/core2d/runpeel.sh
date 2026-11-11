#!/bin/bash
cd /tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/core2d
export PYTHONPATH=/home/dennis/Downloads/acme_303/paper/re
export PYTHONDONTWRITEBYTECODE=1
python3 run_peel.py --load --snap ../snaps/t4500.snap --out fade/pa --ticks 5615 > fade/pa.log 2>&1 &
python3 run_peel.py --load --snap ../snaps/t5800.snap --out fade/pb --ticks 6152 > fade/pb.log 2>&1 &
wait
