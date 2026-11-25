"""Summarise the ESP32 light/deep-sleep and Raspberry Pi Monsoon recordings (.pt5).

Usage: python analyse_sleep_and_pi.py <folder with .pt5 files> <output.json>
Same inferred .pt5 units as analyse_pt5.py (current uA, voltage 0.5 mV).
ESP32: complete wake-to-wake cycles after the startup window (t >= 24 s); awake = 10 ms mean > 20 mA.
Pi: 1 s means; idle = median of a quiet stretch; vision bursts = > idle + 300 mW.
"""
import sys, struct, os, json, numpy as np
d, outjson = sys.argv[1], sys.argv[2]
def load(n):
    b = open(os.path.join(d, n + ".pt5"), "rb").read(); rate = struct.unpack_from("<i", b, 0x44)[0]
    m = (len(b) - 1024) // 6
    s = np.frombuffer(b, dtype=np.dtype([("i", "<i4"), ("v", "<u2")]), count=m, offset=1024)
    ok = (s["v"] != 0xFFFF) & (s["i"] != 32769)
    i = np.where(ok, s["i"]/1000.0, np.nan); v = np.where(ok, s["v"]/2000.0, np.nan)
    return rate, i, v
res = {}
# ---- ESP32 sleep variants: complete cycles after startup (t >= 24 s) ----
for n in ["ESPEverything+10SLightSleep", "ESPEverything+10SDeepSleep"]:
    rate, i, v = load(n); p = i * v
    w = rate // 100; mi = np.nanmean(i[:len(i)//w*w].reshape(-1, w), axis=1)
    awake = mi > 20
    starts = [k for k in range(1, len(awake)) if awake[k] and not awake[k-1] and k/100 >= 24]
    a, b = starts[0] * w, starts[-1] * w          # whole cycles: wake k .. wake k+n
    pc, ic = p[a:b], i[a:b]; ncyc = len(starts) - 1; dur = (b - a) / rate
    sleep_mask = np.repeat(~awake[starts[0]:starts[-1]], w)
    awake_s = np.repeat(awake[starts[0]:starts[-1]], w)
    res[n] = dict(cycles=ncyc, period_s=round(dur/ncyc, 2), avg_P_mW=round(float(np.nanmean(pc)), 1),
        avg_I_mA=round(float(np.nanmean(ic)), 2),
        sleep_I_mA=round(float(np.nanmedian(ic[sleep_mask])), 2), sleep_P_mW=round(float(np.nanmedian(pc[sleep_mask])), 1),
        awake_s_per_cycle=round(float(awake_s.sum()/rate/ncyc), 2), awake_avg_I_mA=round(float(np.nanmean(ic[awake_s])), 1),
        energy_per_cycle_mJ=round(float(np.nansum(pc)/rate/ncyc), 0), V=round(float(np.nanmedian(v)), 3))
    # startup log window (awake, idle) level for reference
    res[n]["startup_window_I_mA"] = round(float(np.nanmedian(i[int(2*rate):int(12*rate)])), 2)
# ---- Pi ----
for n in ["PiBootAndOpenedVNC", "PiBootAndTesttedAISimpleAndComplexe"]:
    rate, i, v = load(n); p = i * v
    w = rate; ms = np.nanmean(i[:len(i)//w*w].reshape(-1, w), axis=1); mp = np.nanmean(p[:len(p)//w*w].reshape(-1, w), axis=1)
    r = dict(duration_s=round(len(i)/rate, 1), V=round(float(np.nanmedian(v)), 3), mean_P_mW=round(float(np.nanmean(p)), 0))
    if n == "PiBootAndOpenedVNC":
        idle = slice(150, 330); r["idle_I_mA"] = round(float(np.median(ms[idle])), 1); r["idle_P_mW"] = round(float(np.median(mp[idle])), 0)
        r["boot_0_110s_energy_J"] = round(float(mp[:110].sum()/1000), 1); r["boot_avg_P_mW"] = round(float(mp[:110].mean()), 0)
        r["vnc_open_P_mW"] = round(float(np.median(mp[400:570])), 0)
    else:
        idle = slice(250, 400); ip = float(np.median(mp[idle])); r["idle_P_mW"] = round(ip, 0); r["idle_I_mA"] = round(float(np.median(ms[idle])), 1)
        r["boot_0_100s_energy_J"] = round(float(mp[:100].sum()/1000), 1)
        hi = mp > ip + 300; bursts = []; k = 110
        while k < len(hi):
            if hi[k]:
                j = k
                while j < len(hi) and (hi[j] or (j+1 < len(hi) and hi[j+1]) or (j+5 < len(hi) and hi[j:j+6].any())): j += 1
                a0, b0 = max(k-2, 0), min(j+2, len(mp))
                bursts.append(dict(start_s=a0, end_s=b0, dur_s=b0-a0, avg_P_mW=round(float(mp[a0:b0].mean()), 0),
                                   peak_mA=round(float(np.nanmax(i[a0*rate:b0*rate])), 0),
                                   energy_above_idle_J=round(float((mp[a0:b0]-ip).sum()/1000), 1)))
                k = j
            k += 1
        r["ai_bursts"] = bursts
    res[n] = r
json.dump(res, open(outjson, "w"), indent=1); print(json.dumps(res, indent=1))
