import sys
import re
import argparse
from datetime import datetime


def str2datetime(s):
    parts = s.split('.')
    dt = datetime.strptime(parts[0], "%Y-%m-%d %H:%M:%S")
    return dt.replace(microsecond=int(parts[1]))


def plot_thr(fname):
    import matplotlib.pyplot as plt
    x = range(len(values))
    y = values
    plt.xlabel(r"time")
    plt.ylabel(r"tx/sec")
    plt.plot(x, y)
    plt.show()
    plt.savefig(fname)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--interval', type=float, default=1, required=False)
    parser.add_argument('--output', type=str, default="hist.png", required=False)
    parser.add_argument('--plot', action='store_true')
    args = parser.parse_args()
    commit_pat = re.compile('([^[].*) \[hotstuff warn\] beacon view ([0-9.]*)$')
    interval = args.interval
    begin_time = None
    next_begin_time = None
    cnt = 0
    lats = []
    timestamps = []
    values = []
    ilats = []
    values_lat = []
    for line in sys.stdin:
        m = commit_pat.match(line)
        if m:
            cnt += 1
            timestamps.append(str2datetime(m.group(1)))


    sorted(timestamps)
    a = timestamps[-1]
    b = timestamps[0]
    c = a - b
    minutes = c.total_seconds() / 60
    print(cnt/minutes)
