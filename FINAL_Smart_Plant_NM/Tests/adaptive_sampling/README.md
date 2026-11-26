# Adaptive sampling (Experiment 2)

How the final firmware (`ESP32/main/adaptive.c`) chooses its sensing interval
and when it wakes the Raspberry Pi. The policy itself is in
`ESP32/main/adaptive_sampling.h`; the same header is compiled here on a PC.

## Files

| File | What it does |
| --- | --- |
| `test_adaptive_sampling.c` | Unit tests of the policy, and a `--simulate` mode that runs a sensor trace through it |
| `simulate_scenario.py` | Builds a 3.5-day sunflower scenario, runs the simulator, compares fixed and adaptive intervals, writes `Results/adaptive_sampling_summary.json` and `Report/figures/adaptive_scenario.pdf` |

## Run

```bash
gcc -std=c11 -O2 -I../../ESP32/main test_adaptive_sampling.c -o test_adaptive_sampling
./test_adaptive_sampling                 # unit tests
python simulate_scenario.py              # needs numpy + matplotlib
```

## What is real and what is simulated

- **Real:** the decision logic (same C code as the firmware) and the energy
  numbers behind the model (Monsoon recordings, see
  `Tests/energy_measurements/energy_model.py`).
- **Simulated:** the sensor values. We could not leave the hardware running
  for several days, so the scenario (drying pot, daily temperature cycle,
  window left open, watering) is synthetic, with noise similar to what we
  measured. Delays are measured from the true (noise-free) threshold crossing.

## Main result (scenario, per day)

| Strategy | Readings | ESP32 DevKit | Soil alarm delay | Window alarm delay |
| --- | ---: | ---: | ---: | ---: |
| fixed 60 s | 1440 | 4.08 kJ | 92 s | 22 s |
| fixed 900 s | 96 | 3.88 kJ | 703 s | 333 s |
| adaptive 60-900 s | 222 | 3.93 kJ | 70 s | 360 s |

The Pi was woken 5 times in 3.5 days (installation, two alarms, two daily photos).
