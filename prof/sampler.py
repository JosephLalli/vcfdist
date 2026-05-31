#!/usr/bin/env python3
"""Throwaway Opt-1 utilization sampler. Launches a command, samples /proc every
~200ms for process utime/stime, live thread count, and summed run_delay, and
timestamps the child's merged stdout/stderr (for stage-banner boundaries)."""
import sys, os, time, subprocess, threading

def read_proc_stat(pid):
    try:
        with open(f'/proc/{pid}/stat', 'rb') as f:
            data = f.read()
        rest = data[data.rfind(b')') + 2:].split()
        return int(rest[11]), int(rest[12])  # utime(f14), stime(f15) in ticks
    except (FileNotFoundError, ProcessLookupError, IndexError, ValueError):
        return None

def count_threads(pid):
    try:
        return len(os.listdir(f'/proc/{pid}/task'))
    except OSError:
        return 0

def sum_run_delay(pid):
    total = 0
    try:
        tids = os.listdir(f'/proc/{pid}/task')
    except OSError:
        return 0
    for tid in tids:
        try:
            with open(f'/proc/{pid}/task/{tid}/schedstat') as f:
                total += int(f.read().split()[1])
        except (OSError, IndexError, ValueError):
            pass
    return total

def main():
    prefix = sys.argv[1]
    cmd = sys.argv[2:]
    util = open(prefix + '.util.csv', 'w')
    util.write('t,utime,stime,nthreads,run_delay_ns\n')
    log = open(prefix + '.log.csv', 'w')
    log.write('t,line\n')

    t0 = time.monotonic()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, bufsize=1, text=True)
    pid = proc.pid

    def reader():
        for line in proc.stdout:
            t = time.monotonic() - t0
            log.write(f'{t:.3f},"{line.rstrip()[:300]}"\n')
            log.flush()
    threading.Thread(target=reader, daemon=True).start()

    nextt = time.monotonic()
    while proc.poll() is None:
        t = time.monotonic() - t0
        st = read_proc_stat(pid)
        if st:
            util.write(f'{t:.3f},{st[0]},{st[1]},{count_threads(pid)},{sum_run_delay(pid)}\n')
            util.flush()
        nextt += 0.2
        s = nextt - time.monotonic()
        if s > 0:
            time.sleep(s)
        else:
            nextt = time.monotonic()
    proc.wait()
    util.close(); log.close()
    print(f'sampler done exit={proc.returncode}')

main()
