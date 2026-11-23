# Energy measurements (Experiments 4 and 5)

Use three separate firmware runs: `unoptimised`, `light`, and `deep`.
Build/flash commands are in [the ESP32 guide](../../ESP32/README.md).

1. Use the same supply voltage, measurement point, sensors and Pi connections
   for every run. Record whether USB is connected and whether the Pi is
   replying. Keep the Pi script and its power state the same across runs.
2. Flash the selected variant. Wait about 30 seconds after reset so startup
   beeps and the optimised firmware's log-access window have finished.
3. Capture at least 60 seconds of steady cycling, ideally the same duration
   as the baseline runs. Repeat three times for each variant. Do not download
   logs during a power recording.
4. Record average current/power and total energy over complete cycles.
   Inspect the low-current sleep sections separately too. Deep-sleep boot,
   sensor reads, UART exchanges and flash writes belong in the cycle average.
   If cycle lengths differ, also compare energy per completed sample.
5. Save recordings with the mode and run number, for example
   `Optimised_Light_1.pt5` and `Optimised_Deep_1.pt5`. Retain the instrument's
   companion files and note the selected firmware revision.
6. After recording, download the ESP32 logs. For either optimised mode,
   press EN, wait about one second, then run the downloader during its
   15-second access window. Separate the measured session from the new reset
   entry using the mode, cycle numbers and system-start entries.

The baseline waits 2500 ms in FreeRTOS; the optimised variants instead sleep
for 2500 ms. All three retain the same two-second Pi receive timeout and
sensor calculations. Deep sleep adds a boot/peripheral-initialisation step.
Sleep variants add mode/cycle and sleep/wake log records; that small logging
cost is included. This is a comparison of these complete firmware variants,
not an isolated measurement of the ESP32 chip's sleep current.

The ESP32 log records events, not current/energy. The external power monitor
provides the electrical measurements. Sensors stay powered throughout, and
the Pi's own energy is included only if it is inside your measurement point.

Hardware checks before accepting a run: readings continue across several
wakes; Pi replies still appear when connected; the buzzer stays silent and
no Pi wake pulse repeats after sleep; logs can be downloaded after reset.
