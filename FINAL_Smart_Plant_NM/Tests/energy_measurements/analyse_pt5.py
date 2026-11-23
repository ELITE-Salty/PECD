"""Summarise Monsoon PowerTool .pt5 recordings (ESP32 energy measurements).

Usage: python analyse_pt5.py <folder with .pt5 files> <output.json>

.pt5 layout used here: header at byte 0 (output voltage float @0x40, sample rate int32 @0x44),
samples from byte 1024, 6 bytes each: int32 main current + uint16 main voltage.
Units are INFERRED, not taken from Monsoon documentation: current in uA (gives ~43 mA idle),
voltage in 0.5 mV (gives 4.99 V at a 5.0 V setting). Samples with voltage 0xFFFF or current
32769 are treated as missing. Cross-check one average against the PowerTool screen.

Per file: the startup 5 s buzzer beep is detected and excluded; idle = median power of the rest;
energy above idle = sum(P) - idle * duration; bursts = 10 ms windows > idle + 8 mA, grouped.
"""
import sys, struct, glob, os, json, numpy as np
d, outjson = sys.argv[1], sys.argv[2]
def load(f):
    b = open(f, "rb").read(); rate = struct.unpack_from("<i", b, 0x44)[0]
    n = (len(b) - 1024) // 6
    s = np.frombuffer(b, dtype=np.dtype([("i", "<i4"), ("v", "<u2")]), count=n, offset=1024)
    ok = (s["v"] != 0xFFFF) & (s["i"] != 32769)
    return rate, np.where(ok, s["i"]/1000.0, np.nan), np.where(ok, s["v"]/2000.0, np.nan)
res = {}
for f in sorted(glob.glob(os.path.join(d, "*.pt5"))):
    rate, i, v = load(f); p = i * v; t = np.arange(len(i)) / rate
    name = os.path.basename(f)[:-4]
    # 100 ms windows to find the startup beep (long run above floor+10)
    floor_all = np.nanpercentile(i, 20)
    w = rate // 10; m = np.nanmean(i[:len(i)//w*w].reshape(-1, w), axis=1)
    hi = m > floor_all + 10
    beep = None; k = 0
    while k < len(hi):
        if hi[k]:
            j = k
            while j < len(hi) and hi[j]: j += 1
            if j - k >= 30: beep = (k/10, j/10); break
            k = j
        k += 1
    start = (beep[1] + 1.0) if beep else 0.0
    sel = (t >= start) & ~np.isnan(i)
    ii, pp, vv = i[sel], p[sel], v[sel]
    dur = sel.sum() / rate
    floor_i = float(np.median(ii)); floor_p = float(np.median(pp))
    # bursts: 10 ms windows above floor+8 mA, merged if gap < 0.6 s -> one cycle = one group starting with a big burst
    w2 = rate // 100; seg = ii[:len(ii)//w2*w2].reshape(-1, w2).mean(axis=1)
    above = seg > floor_i + 8
    idx = np.flatnonzero(above)
    groups = []
    for x in idx:
        if groups and x - groups[-1][1] <= 60: groups[-1][1] = x
        else: groups.append([x, x])
    extra_mJ = float(pp.sum()/rate - floor_p*dur)
    r = dict(duration_s=round(dur,1), excluded_startup_s=round(start,1),
             mean_I_mA=round(float(ii.mean()),2), mean_P_mW=round(float(pp.mean()),1),
             idle_I_mA=round(floor_i,2), idle_P_mW=round(floor_p,1), V=round(float(np.median(vv)),3),
             burst_groups=len(groups), extra_energy_mJ=round(extra_mJ,0),
             burst_peak_mA=round(float(np.percentile(ii, 99.9)),1))
    if beep:
        bs = (t >= beep[0]) & (t < beep[1]) & ~np.isnan(i)
        r["beep_window_s"] = beep; r["beep_mean_I_mA"] = round(float(i[bs].mean()),1)
        r["beep_extra_mJ"] = round(float((p[bs].sum()/rate) - floor_p*bs.sum()/rate),0)
    res[name] = r
json.dump(res, open(outjson, "w"), indent=1); print(json.dumps(res, indent=1))
