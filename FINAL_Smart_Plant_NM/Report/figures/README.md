# Figures

Figures used in the report (`Report/main.tex`). All are generated from the
data in this repository; re-run the scripts to rebuild them.

| Figure | Script |
| --- | --- |
| `power_traces.pdf` | `python Tests/energy_measurements/plot_traces.py Measurements` |
| `energy_vs_interval.pdf` | `python Tests/energy_measurements/energy_model.py` |
| `adaptive_scenario.pdf` | `python Tests/adaptive_sampling/simulate_scenario.py` (compile `test_adaptive_sampling.c` first) |

The architecture diagram is drawn with TikZ directly in `main.tex`.
