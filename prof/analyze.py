#!/usr/bin/env python3
"""Analyze Opt-1 sampler output: cores-busy timeline + per-stage attribution."""
import sys, re, csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

prefix = sys.argv[1]
ANSI = re.compile(r'\x1b\[[0-9;]*m')

# ---- utilization ----
t, cpu, nthr, rd = [], [], [], []
with open(prefix + '.util.csv') as f:
    for row in csv.DictReader(f):
        t.append(float(row['t']))
        cpu.append((int(row['utime']) + int(row['stime'])) / 100.0)  # cpu-seconds
        nthr.append(int(row['nthreads']))
        rd.append(int(row['run_delay_ns']))
t = np.array(t); cpu = np.array(cpu); nthr = np.array(nthr); rd = np.array(rd)
dt = np.diff(t)
cores = np.diff(cpu) / dt                     # avg cores busy in interval
wait = np.clip(np.diff(rd) / dt / 1e9, 0, None)  # approx threads waiting
tm = t[1:]                                    # interval right-edge
wall = t[-1]

# ---- log: banners + final Timers table ----
banners, timers = [], []
tag = re.compile(r'\[(?:[QT] )?\d/8\]|done with precision-recall')
tline = re.compile(r'\[(\d+)\]\s+(.+?)\s*:\s+([\d.]+)s')
with open(prefix + '.log.csv') as f:
    for row in csv.DictReader(f):
        line = ANSI.sub('', row['line'])
        msg = re.sub(r'^\[INFO\s+\S+\s+[\d:]+\]\s*', '', line).strip()
        if tag.search(msg):
            banners.append((float(row['t']), msg[:48]))
        m = tline.search(msg)
        if m:
            timers.append((int(m.group(1)), m.group(2).strip(), float(m.group(3))))

# ---- overall attribution ----
total_core_s = np.sum(cores * dt)
ideal = 64 * wall
print(f'\n=== {prefix} ===')
print(f'wall {wall:.1f}s | core-seconds {total_core_s:.0f} | ideal(64x) {ideal:.0f} '
      f'| overall efficiency {total_core_s/ideal*100:.1f}% | mean cores {total_core_s/wall:.1f}/64')
# time-in-busy-band histogram (where the idle lives)
bands = [(0,2),(2,8),(8,24),(24,48),(48,65)]
print('time spent by cores-busy band:')
for lo,hi in bands:
    sec = np.sum(dt[(cores>=lo)&(cores<hi)])
    print(f'  {lo:2d}-{hi:2d} cores: {sec:6.1f}s ({sec/wall*100:4.1f}%)')

# ---- final Timers table (authoritative per-stage wall) ----
if timers:
    print('per-stage wall (program timers):')
    for i,name,sec in timers:
        print(f'  [{i}] {name:18s} {sec:8.2f}s ({sec/wall*100:4.1f}%)')

# ---- per inter-banner segment: mean cores ----
print('per-segment mean cores-busy:')
segs = banners + [(wall, 'END')]
for (t0,name),(t1,_) in zip(segs, segs[1:]):
    mask = (tm>=t0)&(tm<t1)
    if mask.sum()==0: continue
    mc = np.sum(cores[mask]*dt[mask])/max(np.sum(dt[mask]),1e-9)
    print(f'  {t0:6.1f}-{t1:6.1f}s ({t1-t0:5.1f}s)  cores~{mc:5.1f}/64  {name}')

# ---- plot ----
fig, ax = plt.subplots(figsize=(15,5))
ax.fill_between(tm, 0, cores, color='steelblue', alpha=0.6, label='cores busy')
ax.plot(tm, nthr[1:], color='darkorange', lw=0.8, label='live OS threads')
ax.axhline(64, color='gray', ls=':', lw=1)
for bt,name in banners:
    ax.axvline(bt, color='crimson', lw=0.6, alpha=0.5)
    ax.text(bt, 66, name, rotation=90, fontsize=6, va='bottom', color='crimson')
ax.set_xlabel('wall time (s)'); ax.set_ylabel('count')
ax.set_ylim(0, 80); ax.set_xlim(0, wall)
ax.set_title(f'{prefix}  wall {wall:.0f}s  eff {total_core_s/ideal*100:.0f}%  mean {total_core_s/wall:.1f}/64 cores')
ax.legend(loc='upper right')
plt.tight_layout()
plt.savefig(prefix + '.png', dpi=110)
print(f'wrote {prefix}.png')
