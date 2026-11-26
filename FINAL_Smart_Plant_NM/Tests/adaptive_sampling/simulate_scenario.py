"""Scenario evaluation of the adaptive sensing policy (Experiment 2).

The scenario is SIMULATED (we could not leave the hardware running for days).
Only the sensor values are synthetic; the decisions come from the real firmware
logic (ESP32/main/adaptive_sampling.h, compiled into test_adaptive_sampling).
Energy comes from the measurement-based model (energy_model.py).

Scenario: a dwarf sunflower (Helianthus annuus 'Sunspot') indoors, 3.5 days.
  - soil starts freshly watered (900/1000) and dries faster in daytime;
    it falls below the soil alarm (300) on day 3 and is watered 4 h later;
  - room temperature 18.5-24.5 degC daily cycle, humidity moves opposite;
  - day 2, 06:00: a window is left open for 1 h -> 8 degC (cold alarm);
  - sensor noise: soil sigma 3/1000 (we measured about +-6/1000 in wet soil),
    DHT11 temperature sigma 0.2 degC, humidity sigma 1 %RH.
Strategies compared: fixed 2.5 s (baseline firmware), fixed 10 s (measured
sleep firmware), fixed 60 s and 900 s, and adaptive 60-900 s.

Usage (from this folder, after compiling test_adaptive_sampling, see the .c file):
    python simulate_scenario.py
"""
import json
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "Tests", "energy_measurements"))
import energy_model as em  # noqa: E402

STEP = 10                      # trace resolution (s)
HOURS = 84
START_H = 8.0                  # t = 0 is 08:00
SOIL_ALARM = 300.0
COLD_ALARM = 10.0


def make_trace(seed=1):
    rng = np.random.default_rng(seed)
    t = np.arange(0, HOURS * 3600 + 1, STEP, dtype=float)
    hod = (START_H + t / 3600) % 24
    day = (hod >= 8) & (hod < 20)
    rate = np.where(day, 15.0, 6.0) / 3600          # drying, units per second
    soil = 900 - np.cumsum(rate) * STEP
    cross = np.argmax(soil < SOIL_ALARM)
    water = cross + int(4 * 3600 / STEP)            # owner waters 4 h later
    soil[water:] = 900 - np.cumsum(rate[water:]) * STEP
    temp = 21.5 + 3.0 * np.sin(2 * np.pi * (hod - 9) / 24)
    window = (t >= (24 + 6 - START_H) * 3600) & (t < (24 + 7 - START_H) * 3600)
    temp[window] = 8.0
    hum = 55 - 1.5 * (temp - 21.5)
    soil_n = np.clip(soil + rng.normal(0, 3, t.size), 0, 1000)
    temp_n = np.round(temp + rng.normal(0, 0.2, t.size), 1)
    hum_n = np.round(hum + rng.normal(0, 1.0, t.size))
    # Detection delays are measured from the moment the TRUE (noise-free)
    # soil value crosses the threshold, i.e. when the plant really is dry.
    events = dict(soil_cross_s=float(t[cross]),
                  watering_s=float(t[water]), cold_start_s=float(t[window][0]))
    return t, soil_n, temp_n, hum_n, events


def run_adaptive(trace_csv):
    exe = os.path.join(HERE, "test_adaptive_sampling.exe" if os.name == "nt" else "test_adaptive_sampling")
    out = subprocess.run([exe, "--simulate", trace_csv], check=True, capture_output=True, text=True).stdout
    rows = [line.split(",") for line in out.strip().splitlines()[1:]]
    return np.array(rows, dtype=float)              # t, soil, temp, hum, interval, wake, alarm


def first_detection(times, values, start_s, test):
    for tt, v in zip(times, values):
        if tt >= start_s and test(v):
            return round(float(tt - start_s))
    return float("nan")


def main():
    t, soil, temp, hum, ev = make_trace()
    csv = os.path.join(HERE, "scenario_trace.csv")
    with open(csv, "w") as f:
        f.write("t_s,soil,temp,hum\n")
        for row in zip(t, soil, temp, hum):
            f.write("%.0f,%.1f,%.1f,%.1f\n" % row)
    a = run_adaptive(csv)
    total = t[-1]
    days = total / 86400

    n_wake_pi = int(a[:, 5].sum())
    res = {"scenario_hours": HOURS, "events": ev, "strategies": {}}

    def detection(times, idx_soil, idx_temp):
        return dict(
            soil_alarm_delay_s=first_detection(times, idx_soil, ev["soil_cross_s"], lambda v: v < SOIL_ALARM),
            cold_alarm_delay_s=first_detection(times, idx_temp, ev["cold_start_s"], lambda v: v < COLD_ALARM))

    # fixed strategies: sample the trace every T seconds
    for name, T in (("fixed_2.5s_baseline", 2.5), ("fixed_10s", 10), ("fixed_60s", 60), ("fixed_900s", 900)):
        # Phase offset so the fixed sampling grid is not accidentally aligned
        # with the scenario events (which start on whole hours).
        ts = np.arange(0.37 * T, total + 1, T)
        k = np.minimum((ts // STEP).astype(int), t.size - 1)
        det = detection(ts, soil[k], temp[k])
        if T == 2.5:
            e_day = em.P_BASELINE * 86400 / 1000      # measured, always awake
        else:
            e_day = float(em.avg_power(T, em.P_SLEEP_DEEP, em.P_AWAKE_DEEP, em.T_AWAKE_DEEP)) * 86400 / 1000
        bare = (None if T == 2.5 else round(float(em.avg_power(T, em.P_SLEEP_MODULE, em.P_AWAKE_LIGHT,
                                                               em.T_AWAKE_LIGHT)) * 86400 / 1e6, 3))
        res["strategies"][name] = dict(readings_per_day=round(ts.size / days), esp32_kJ_per_day=round(e_day / 1000, 2),
                                       bare_module_kJ_per_day=bare, **det)

    det = detection(a[:, 0], a[:, 1], a[:, 2])
    e_adapt = em.esp_energy_J(len(a), total, n_wake_pi) / days
    iv = a[:, 4]
    awake_bare = len(a) * em.T_AWAKE_LIGHT + n_wake_pi * em.PI_SESSION_ESP_AWAKE_S
    bare_adapt = (em.P_SLEEP_MODULE * (total - awake_bare) + em.P_AWAKE_LIGHT * awake_bare) / 1000 / days
    res["strategies"]["adaptive_60_900s"] = dict(
        readings_per_day=round(len(a) / days), esp32_kJ_per_day=round(e_adapt / 1000, 2),
        bare_module_kJ_per_day=round(bare_adapt / 1000, 3),
        median_interval_s=float(np.median(iv)),
        share_of_time_at_max_interval=round(float(iv[iv == 900].sum() / iv.sum()), 2), **det)
    res["pi"] = dict(wakes=n_wake_pi, wakes_per_day=round(n_wake_pi / days, 2),
                     wake_times_h=[round(x / 3600, 1) for x in a[a[:, 5] == 1, 0]],
                     reasons="t=0 installation photo; then daily photos and an extra photo when a new alarm starts (>= 6 h after the previous photo)")
    out_json = os.path.join(ROOT, "Results", "adaptive_sampling_summary.json")
    with open(out_json, "w") as f:
        json.dump(res, f, indent=1)
    print(json.dumps(res, indent=1))

    # ---------------- figure ----------------
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.size": 8, "legend.fontsize": 7, "font.family": "serif", "pdf.fonttype": 42})
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(4.8, 2.45), sharex=True,
                                   gridspec_kw=dict(height_ratios=[1.3, 1]))
    h = t / 3600
    ax1.plot(h, soil, color="#8B5A2B", lw=0.8, label="soil moisture (0-1000)")
    ax1.axhline(SOIL_ALARM, color="#8B5A2B", lw=0.6, ls="--")
    ax1.text(1, SOIL_ALARM + 25, "soil alarm < 300", fontsize=7, color="#8B5A2B")
    ax1.set_ylabel("soil moisture")
    ax1.set_ylim(0, 1000)
    axt = ax1.twinx()
    axt.plot(h, temp, color="#2A78D6", lw=0.6, label="air temperature")
    axt.set_ylabel("temperature (°C)", color="#2A78D6")
    axt.set_ylim(0, 30)
    axt.tick_params(axis="y", colors="#2A78D6")
    for x in a[a[:, 5] == 1, 0] / 3600:
        ax1.axvline(x, color="#2C5F2D", lw=0.8, ls=":")
    ax1.text(a[a[:, 5] == 1, 0][1] / 3600 + 0.5, 920, "Pi photo", fontsize=7, color="#2C5F2D")
    ax1.annotate("window open\n(8 °C, 1 h)", (ev["cold_start_s"] / 3600, 280), (ev["cold_start_s"] / 3600 + 4, 120),
                 fontsize=7, arrowprops=dict(arrowstyle="-", lw=0.6))
    ax1.annotate("watered", (ev["watering_s"] / 3600, 880), (ev["watering_s"] / 3600 + 3, 700),
                 fontsize=7, arrowprops=dict(arrowstyle="-", lw=0.6))
    ax2.step(a[:, 0] / 3600, a[:, 4], where="post", color="#EB6834", lw=0.9)
    ax2.set_yscale("log")
    ax2.set_yticks([60, 120, 240, 480, 900])
    ax2.set_yticklabels(["60", "120", "240", "480", "900"])
    ax2.set_ylabel("next interval (s)")
    ax2.set_xlabel("time (h), t = 0 at 08:00")
    ax2.set_xlim(0, HOURS)
    for ax in (ax1, ax2):
        ax.grid(True, lw=0.3, alpha=0.6)
    fig.tight_layout(h_pad=0.4)
    fig.savefig(os.path.join(ROOT, "Report", "figures", "adaptive_scenario.pdf"))


if __name__ == "__main__":
    main()
