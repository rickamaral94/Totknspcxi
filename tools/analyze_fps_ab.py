#!/usr/bin/env python3
"""Summarize vk-prof logs for LARECOMP_AB=<block-frames>:<experiment>."""
import argparse
import json
import re
import statistics
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=Path)
parser.add_argument('--block-frames', type=int, default=240)
parser.add_argument('--warmup-seconds', type=float, default=55)
parser.add_argument('--min-draws', type=int, default=100000)
args = parser.parse_args()
if args.block_frames <= 0:
    parser.error('--block-frames must be positive')
pattern = re.compile(r't=([\d.]+) frame (\d+) ([\d.]+) fps.*?\((\d+) draws.*?upload ([\d.]+)MB.*?cpu exec (\d+).*?vtx (\d+)\).*?cbuf (\d+) ms')
groups = {0: [], 1: []}
previous = None
for line in args.log.read_text(encoding='utf-8').splitlines():
    match = pattern.search(line)
    if not match:
        continue
    seconds, frame, fps, draws, upload, cpu, vertex, cbuf = map(float, match.groups())
    block = int(frame) // args.block_frames
    if previous is not None and frame > previous and seconds >= args.warmup_seconds and draws >= args.min_draws:
        if int(previous) // args.block_frames == block:
            count = frame - previous
            groups[block % 2].append(dict(fps=fps, cpu_ms=cpu / count, vertex_ms=vertex / count,
                                         cbuf_ms=cbuf / count, upload_mb=upload / count))
    previous = frame
result = {}
for mode, rows in groups.items():
    result[mode] = dict(samples=len(rows))
    if rows:
        result[mode].update({key: round(statistics.mean(row[key] for row in rows), 3) for key in rows[0]})
print(json.dumps(result, indent=2))
