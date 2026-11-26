"""Energy model of the ESP32 board, built only from our Monsoon measurements.

Why a model: the adaptive firmware (ESP32/main/adaptive.c) could not be measured
on the Monsoon before the deadline. Instead we split the measured light/deep-sleep
cycles into their parts (sleep power, awake power, awake time) and recombine them
for other sleep intervals. The 2 s UART wait that the measured firmware spent in
every cycle is removed, because adaptive.c only talks to the Pi when it is awake.

Average power for a sleep interval T (s):
    P(T) = (P_sleep * T + P_awake * t_awake) / (T + t_awake)

Inputs (Results/energy_consumption/*.json, produced by analyse_pt5.py and
analyse_sleep_and_pi.py):
    P_sleep, P_awake, t_awake  - ESPEverything+10SLightSleep / ...DeepSleep
    baseline average power     - UnOptimised1..3 (ESP32 always awake)
One extra input is NOT ours: the ESP32 module light-sleep current from the
Espressif datasheet (0.8 mA), used only for the "bare module" projection.

Usage: python energy_model.py   (writes the figure and a JSON summary)
"""
import json
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
RES = os.path.join(ROOT, "Results", "energy_consumption")
FIG = os.path.join(ROOT, "Report", "figures")

UART_WAIT_S = 2.0          # PI_READ_TIMEOUT_MS in optimised.c (Pi not replying)
DATASHEET_LIGHT_SLEEP_mA = 0.8   # ESP32 datasheet, light sleep (module only)
MODULE_V = 3.3
PI_WAKE_J = 300.0          # boot ~200 J (measured) + photo/vision/shutdown, rounded up
PI_SESSION_ESP_AWAKE_S = 120.0   # ESP32 stays awake while serving the Pi (74 s boot + cycle)


def _load():
    with open(os.path.join(RES, "esp32_sleep_and_pi_summary.json")) as f:
        sleep = json.load(f)
    with open(os.path.join(RES, "esp32_unoptimised_summary.json")) as f:
        base = json.load(f)
    return sleep, base


SLEEP, BASE = _load()
_L = SLEEP["ESPEverything+10SLightSleep"]
_D = SLEEP["ESPEverything+10SDeepSleep"]
_PI = SLEEP["PiBootAndTesttedAISimpleAndComplexe"]

P_SLEEP_LIGHT = _L["sleep_P_mW"]
P_SLEEP_DEEP = _D["sleep_P_mW"]
P_AWAKE_LIGHT = _L["awake_avg_I_mA"] * _L["V"]
P_AWAKE_DEEP = _D["awake_avg_I_mA"] * _D["V"]
T_AWAKE_LIGHT_MEAS = _L["awake_s_per_cycle"]
T_AWAKE_DEEP_MEAS = _D["awake_s_per_cycle"]
T_AWAKE_LIGHT = T_AWAKE_LIGHT_MEAS - UART_WAIT_S
T_AWAKE_DEEP = T_AWAKE_DEEP_MEAS - UART_WAIT_S
P_BASELINE = float(np.mean([BASE[k]["mean_P_mW"] for k in ("UnOptimised1", "UnOptimised2", "UnOptimised3")]))
P_SLEEP_MODULE = DATASHEET_LIGHT_SLEEP_mA * MODULE_V
P_PI_IDLE_W = _PI["idle_P_mW"] / 1000.0
E_CYCLE_LIGHT_mJ = P_AWAKE_LIGHT * T_AWAKE_LIGHT


def avg_power(T, p_sleep, p_awake, t_awake):
    """Average board power (mW) when sleeping T s between wake-ups."""
    T = np.asarray(T, dtype=float)
    return (p_sleep * T + p_awake * t_awake) / (T + t_awake)


def esp_energy_J(n_readings, total_s, n_pi_sessions=0, deep=True):
    """Energy (J) of the adaptive firmware (deep sleep by default, as in
    adaptive.c) over total_s with n_readings wake-ups and n_pi_sessions."""
    ps, pa, ta = ((P_SLEEP_DEEP, P_AWAKE_DEEP, T_AWAKE_DEEP) if deep
                  else (P_SLEEP_LIGHT, P_AWAKE_LIGHT, T_AWAKE_LIGHT))
    awake = n_readings * ta + n_pi_sessions * PI_SESSION_ESP_AWAKE_S
    return (ps * (total_s - awake) + pa * awake) / 1000.0


def check_decomposition():
    """The parts must give back the measured averages (sanity check, not a
    validation: they come from the same recordings)."""
    out = {}
    for name, r, ps, pa in (("light", _L, P_SLEEP_LIGHT, P_AWAKE_LIGHT),
                            ("deep", _D, P_SLEEP_DEEP, P_AWAKE_DEEP)):
        t_sleep = r["period_s"] - r["awake_s_per_cycle"]
        pred = float(avg_power(t_sleep, ps, pa, r["awake_s_per_cycle"]))
        out[name] = dict(predicted_mW=round(pred, 1), measured_mW=r["avg_P_mW"],
                         error_pct=round(100 * (pred - r["avg_P_mW"]) / r["avg_P_mW"], 1))
    return out


def main():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update({"font.size": 8, "axes.titlesize": 8, "legend.fontsize": 7,
                         "font.family": "serif", "pdf.fonttype": 42})
    T = np.logspace(np.log10(1), np.log10(900), 200)
    curves = {
        "light sleep, 2 s UART wait (measured firmware)":
            avg_power(T, P_SLEEP_LIGHT, P_AWAKE_LIGHT, T_AWAKE_LIGHT_MEAS),
        "light sleep, no UART wait":
            avg_power(T, P_SLEEP_LIGHT, P_AWAKE_LIGHT, T_AWAKE_LIGHT),
        "deep sleep, no UART wait (adaptive.c)":
            avg_power(T, P_SLEEP_DEEP, P_AWAKE_DEEP, T_AWAKE_DEEP),
        "bare module, light sleep (datasheet projection)":
            avg_power(T, P_SLEEP_MODULE, P_AWAKE_LIGHT, T_AWAKE_LIGHT),
    }
    styles = [("#2A78D6", "--"), ("#2A78D6", "-"), ("#EB6834", "-"), ("#555555", ":")]

    fig, ax = plt.subplots(figsize=(4.8, 2.05))
    for (label, y), (c, ls) in zip(curves.items(), styles):
        ax.plot(T, y, color=c, ls=ls, lw=1.4, label=label)
    ax.axhline(P_BASELINE, color="#B22222", lw=1.0)
    ax.text(1.1, P_BASELINE * 1.07, f"baseline, always awake: {P_BASELINE:.0f} mW (measured)",
            fontsize=7, color="#B22222")
    ax.plot([_L["period_s"] - _L["awake_s_per_cycle"]], [_L["avg_P_mW"]], "o", color="#2A78D6", ms=4)
    ax.plot([_D["period_s"] - _D["awake_s_per_cycle"]], [_D["avg_P_mW"]], "s", color="#EB6834", ms=4)
    ax.axvspan(60, 900, color="#97BC62", alpha=0.18, lw=0)
    ax.text(70, 1.15, "adaptive range 60-900 s", fontsize=7, color="#2C5F2D")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlim(1, 900); ax.set_ylim(1, 400)
    ax.set_xlabel("sleep interval between readings T (s)")
    ax.set_ylabel("average power (mW)")
    ax.grid(True, which="major", lw=0.3, alpha=0.6)
    ax.legend(loc="lower left", frameon=False, bbox_to_anchor=(0.0, 0.06))
    fig.tight_layout()
    os.makedirs(FIG, exist_ok=True)
    fig.savefig(os.path.join(FIG, "energy_vs_interval.pdf"))

    pts = {}
    for T0 in (10, 60, 240, 900):
        pts[str(T0)] = {k: round(float(avg_power(T0, *a)), 1) for k, a in {
            "light_meas_fw": (P_SLEEP_LIGHT, P_AWAKE_LIGHT, T_AWAKE_LIGHT_MEAS),
            "light_adaptive": (P_SLEEP_LIGHT, P_AWAKE_LIGHT, T_AWAKE_LIGHT),
            "deep_adaptive": (P_SLEEP_DEEP, P_AWAKE_DEEP, T_AWAKE_DEEP),
            "bare_module": (P_SLEEP_MODULE, P_AWAKE_LIGHT, T_AWAKE_LIGHT)}.items()}
    summary = dict(
        inputs=dict(P_sleep_light_mW=P_SLEEP_LIGHT, P_sleep_deep_mW=P_SLEEP_DEEP,
                    P_awake_light_mW=round(P_AWAKE_LIGHT, 1), P_awake_deep_mW=round(P_AWAKE_DEEP, 1),
                    t_awake_light_measured_s=T_AWAKE_LIGHT_MEAS, t_awake_light_without_uart_s=round(T_AWAKE_LIGHT, 2),
                    t_awake_deep_without_uart_s=round(T_AWAKE_DEEP, 2),
                    E_cycle_light_without_uart_mJ=round(E_CYCLE_LIGHT_mJ, 0),
                    P_baseline_mW=round(P_BASELINE, 1), P_sleep_module_datasheet_mW=P_SLEEP_MODULE,
                    P_pi_idle_W=P_PI_IDLE_W, E_pi_wake_J=PI_WAKE_J),
        decomposition_check=check_decomposition(),
        avg_power_mW_at_interval_s=pts,
    )
    with open(os.path.join(RES, "energy_model_summary.json"), "w") as f:
        json.dump(summary, f, indent=1)
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
