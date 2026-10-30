#!/bin/bash
# analyse_all.sh <prefix...> — summarise %TEMP%/gc-<prefix>-*.csv traces
cd /mnt/c/Users/User/AppData/Local/Temp
for f in "$@"; do
  python3 -I /home/deby/projects/gate-automation/app/tools/motion-capture/trace_analyse.py gc-$f-frames.csv gc-$f-items.csv $f > gc-$f-analysis.json
  python3 -c "
import json; d=json.load(open('gc-$f-analysis.json'))
print(d['label'], 'p90',d['p90_frame_ms'],'max',d['max_frame_ms'],'dropped60',d['dropped_frames_60hz'],'over16.7',d['frames_over_16_7ms'][:10],'card steps',d['card_steps_ms'])
print('   issues', d['issues'][:6])"
done
