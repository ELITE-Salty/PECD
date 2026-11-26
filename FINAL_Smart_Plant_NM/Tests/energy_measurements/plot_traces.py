"""Figure of the measured Monsoon traces used in the report.

(a) ESP32 baseline firmware (UnOptimised2), 10 s, 10 ms averages.
(b) ESP32 light-sleep firmware, one full cycle, 10 ms averages.
(c) Raspberry Pi 3B: boot, idle and the two vision models, 1 s averages.

Usage: python plot_traces.py <folder with .pt5 files>
Same inferred .pt5 units as analyse_pt5.py (current uA, voltage 0.5 mV).
"""
import os
import struct
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
FIG = os.path.normpath(os.path.join(HERE, "..", "..", "Report", "figures"))


def load(folder, name):
    b = open(os.path.join(folder, name + ".pt5"), "rb").read()
    rate = struct.unpack_from("<i", b, 0x44)[0]
    n = (len(b) - 1024) // 6
    s = np.frombuffer(b, dtype=np.dtype([("i", "<i4"), ("v", "<u2")]), count=n, offset=1024)
    ok = (s["v"] != 0xFFFF) & (s["i"] != 32769)
    i = np.where(ok, s["i"] / 1000.0, np.nan)      # mA
    v = np.where(ok, s["v"] / 2000.0, np.nan)      # V
    return rate, i * v                              # mW


def window_mean(p, w):
    m = len(p) // w * w
    blocks = p[:m].reshape(-1, w)
    keep = ~np.all(np.isnan(blocks), axis=1)       # drop windows with no valid sample
    return np.nanmean(blocks[keep], axis=1)


def main(folder):
    plt.rcParams.update({"font.size": 8, "font.family": "serif", "pdf.fonttype": 42})
    fig, axes = plt.subplots(1, 3, figsize=(4.8 * 1.0, 1.9),
                             gridspec_kw=dict(width_ratios=[1, 1, 1.25]))

    rate, p = load(folder, "UnOptimised2")
    w = rate // 100
    pm = window_mean(p, w)
    t0 = 60.0
    sel = slice(int(t0 * 100), int((t0 + 10) * 100))
    ax = axes[0]
    ax.plot(np.arange(pm[sel].size) / 100, pm[sel], color="#B22222", lw=0.7)
    ax.set_title("(a) ESP32 baseline", fontsize=8)
    ax.set_xlabel("time (s)"); ax.set_ylabel("power (mW)")
    ax.set_ylim(0, 400)

    rate, p = load(folder, "ESPEverything+10SLightSleep")
    w = rate // 100
    pm = window_mean(p, w)
    awake = pm > 100
    starts = [k for k in range(1, len(awake)) if awake[k] and not awake[k - 1] and k / 100 >= 24]
    a = starts[1] - 100
    ax = axes[1]
    seg = pm[a:a + 1400]
    ax.plot(np.arange(seg.size) / 100, seg, color="#2A78D6", lw=0.7)
    ax.set_title("(b) ESP32 light sleep", fontsize=8)
    ax.set_xlabel("time (s)")
    ax.set_ylim(0, 400)
    ax.annotate("awake 2.5 s\n(2 s UART wait)", (2.2, 215), (4.5, 300), fontsize=6.5,
                arrowprops=dict(arrowstyle="-", lw=0.5))
    ax.annotate("asleep 51 mW", (8, 55), (6, 120), fontsize=6.5, arrowprops=dict(arrowstyle="-", lw=0.5))

    rate, p = load(folder, "PiBootAndTesttedAISimpleAndComplexe")
    pm = window_mean(p, rate) / 1000.0              # W, 1 s
    ax = axes[2]
    ax.plot(np.arange(pm.size), pm, color="#2C5F2D", lw=0.7)
    ax.set_title("(c) Raspberry Pi 3B", fontsize=8)
    ax.set_xlabel("time (s)"); ax.set_ylabel("power (W)")
    ax.set_ylim(0, 4)
    ax.text(15, 3.55, "boot", fontsize=6.5)
    ax.text(150, 2.0, "S", fontsize=6.5, ha="center")
    ax.text(450, 3.55, "L", fontsize=6.5, ha="center")
    ax.text(590, 3.55, "L", fontsize=6.5, ha="center")
    ax.text(230, 0.65, "idle 1.25 W", fontsize=6.5)
    for ax in axes:
        ax.grid(True, lw=0.3, alpha=0.6)
    fig.tight_layout(w_pad=0.6)
    os.makedirs(FIG, exist_ok=True)
    fig.savefig(os.path.join(FIG, "power_traces.pdf"))


if __name__ == "__main__":
    main(sys.argv[1])
