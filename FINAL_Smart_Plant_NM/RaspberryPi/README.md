# Raspberry Pi plant monitoring cycle

`plant_cycle.py` captures one photo, runs colour screening with the small model,
optionally runs the larger classifier, requests an ESP32 alarm when indicated,
and saves the ESP32 logs. A systemd service can run it at boot and power off
afterwards. The Python program itself never shuts down the machine.

## Inspected Pi and models

On 2026-11-19, `kopin@pecd-nk.local` reported **Raspberry Pi 3 Model B Rev 1.2**
(not Pi 4), Debian 13 aarch64, Python 3.13 and an **IMX219 ribbon camera**.

The integration imports the completed code already on the Pi:
- `~/Desktop/leafs-starter/main.py`: U2-Net-p foreground segmentation and
  colour rules, using `u2netp.onnx` and `helpers.py`.
- `~/Desktop/AI_base/main.py`: 15-class pepper/potato/tomato classifier,
  with its original preprocessing, temperature scaling and labels.
- `~/Desktop/.venv`: existing inference environment, plus pyserial 3.5.

These lab folders and model weights remain unchanged and must remain present.
Their `ASSETS.json` files record model hashes and provenance; the small
model's export provenance/license is not recorded in the supplied assets.
The old `~/Desktop/Something.py` is only a UART handshake demo; it must not run
at the same time as this integration on the same port.

## Decision policy

Thresholds in `config.json` are provisional and need real-camera calibration.

1. All regions ≥90% green and ≤5% combined yellow/brown: healthy-ish; skip
   the large model.
2. Any region ≤35% green and ≥40% yellow/brown: stressed-ish; request four
   beeps without needing a disease diagnosis.
3. Other detected regions: use the large model only when `plant_species` is
   explicitly `pepper`, `potato`, or `tomato`. The default is `unknown`.
4. Any unhealthy score ≥0.80 triggers the alarm; all healthy scores ≥0.80
   produce healthy. Otherwise the result remains uncertain.
5. No foreground means unknown, never healthy. More than eight regions
   skips the classifier as uncertain to bound the cycle's work.

Foreground regions are not guaranteed to be leaves. Touching leaves can merge;
aim at isolated leaves with a simple background and stable lighting. Green
leaves can still be diseased. The classifier covers only its listed species and
diseases. Yellow/brown masks in the supplied helper overlap, so their sum is a
screening score, not a disjoint damaged-area percentage.

## Running

Installed on the Pi in `~/plant-monitor`. Confirm species and serial port in
`config.json` before operating real alarms.

```bash
cd ~/plant-monitor
# Existing image; no camera or serial access:
~/Desktop/.venv/bin/python plant_cycle.py --offline \
  --image ~/Desktop/leafs-starter/images/pepper_healthy.jpg
# Fresh camera photo; no serial access:
~/Desktop/.venv/bin/python plant_cycle.py --offline
# Logs only; no camera, models, alarms or shutdown:
~/Desktop/.venv/bin/python plant_cycle.py --logs-only
# Full cycle; requires updated ESP32 firmware and wiring:
~/Desktop/.venv/bin/python plant_cycle.py
```

Each unique directory under `~/plant-monitor/runs/` contains the photo,
overlay, mask, optional classifier crops, config snapshot and `result.json`.
Successful serial collection adds `events_prev.txt` when available and
`events_log.txt`. These are full snapshots, not incremental appends: the ESP32
has uptime timestamps and rotation but no persistent transfer cursor.
Repeated measurements across snapshots are expected.

Transfer errors leave `.partial` files, never apparently complete exports.
Camera/inference failures still allow log collection. Beep completion requires
an acknowledgement; a lost acknowledgement is recorded and is not blindly
retried. Log collection is attempted even after an alarm failure. Runtime errors
return exit code 1. All run folders are retained; archive or remove old runs
before storage fills.

## Serial connection

115200 baud, 8N1, no flow control. Updated ESP32 firmware accepts commands on
UART0 (USB bridge) and UART2 (GPIO). Replies return on the requesting port.

| Command | Response |
| --- | --- |
| `PING` | `@@PLANT\|READY` |
| `BEEP 4` | `@@PLANT\|BEEP\|4\|DONE` after completing the beeps |
| `logs` | BEGIN/LINE/END or MISSING per file, then `@@PLANT\|DONE` |
| `WAKE_STATUS` | `@@PLANT\|WAKE\|<seconds until next wake>` |
| `WAKE_IN 60` | Schedule the next wake in 60 seconds; replies `@@PLANT\|WAKE\|60` |

Errors use `@@PLANT|ERROR|...`. Beep counts must be 1..10. Unknown/oversized
commands are rejected. Sensor sampling pauses during exports and beeps in
this single-loop firmware. Full exports can take over a minute; the configured
serial deadline is 120 seconds per operation.

Proposed wiring (verify the actual ESP32 module first):

| Raspberry Pi | ESP32 |
| --- | --- |
| GPIO14 TX, physical pin 8 | GPIO16 RX, UART2 |
| GPIO15 RX, physical pin 10 | GPIO17 TX, UART2 |
| GND, physical pin 6 | GND |

Both UART sides use **3.3 V logic**, never 5 V. The ESP32 needs independent
power to monitor while the Pi is off. Some ESP32 modules use GPIO16/17 for
PSRAM; confirm they are available.

The inspected Pi has UART enabled, `/dev/serial0 -> ttyS0`, and no serial
console in its boot command line. Ensure no getty or demo owns the port.
The mini UART baud rate depends on core clock: validate under load or configure
PL011 appropriately before unattended operation. For USB, configure a
`/dev/serial/by-id/...` path instead. Some USB bridges reset the ESP32 on open
despite inactive RTS/DTR; UART2 avoids that risk.

## Startup, shutdown and wake

The supplied `plant-cycle.service` is **not installed/enabled**: the Pi requires
an interactive sudo password. GPIO wake has been verified, but the post-wake
serial transfer stopped responding and must be restored before unattended use.
The unit includes automatic shutdown. After resolving the UART issue, run
these commands in a Pi terminal and enter the sudo password there:

```bash
sudo install -m 644 ~/plant-monitor/plant-cycle.service /etc/systemd/system/plant-cycle.service
sudo systemctl daemon-reload
sudo systemctl enable plant-cycle.service
```

This runs the full cycle at the next boot and powers off after success or
failure, including the eight-minute timeout. Enabling the service does not run
it immediately. Starting it manually with `sudo systemctl start plant-cycle`
will run the cycle and disconnect SSH when shutdown occurs.

Create `~/plant-monitor/maintenance` before a reboot to skip the cycle and keep
SSH available. Remove it to resume. Disable startup with
`sudo systemctl disable plant-cycle.service`.

The new ESP32 firmware schedules a wake pulse every **24 hours of ESP32 uptime**.
The first pulse is 24 hours after ESP32 startup; restarting the ESP32 restarts
the countdown. This is not a fixed calendar time, and it does not persist
across power loss. Each pulse pulls GPIO25 low for 250 ms in open-drain mode,
then releases it. If a deadline was missed, one pulse is sent without a burst
of retries. `WAKE_STATUS` reports the remaining seconds. `WAKE_IN 60` schedules
one accelerated test (allowed range 30..86400 seconds); subsequent pulses
return to the normal 24-hour interval.

With both boards unpowered, add **ESP32 GPIO25 → Pi GPIO3 (physical pin 5)**.
Keep the existing common ground and TX/RX connections. Pi physical pin 3 is
GPIO2: do not confuse GPIO numbers with header pin numbers. The Pi retains its
normal power supply while halted; the ESP32 must remain powered to keep time.
The wake output is open-drain and never actively drives the Pi pin high. Do not
enable a `gpio-shutdown` overlay on that pin: an extra pulse while the Pi is
running could otherwise request an unintended shutdown. No such overlay or
I2C-on-GPIO3 configuration was active on the inspected Pi.

The daily sequence will be: ESP32 wake pulse → Pi boots → boot service takes a
photo and analyses it → optional beep request → logs saved → Pi powers off.
The Pi's service runs on every boot, including manual boots; use the maintenance
file to skip it when needed. The firmware scheduler is **flashed**, the extra
wake wire is connected, and a 60-second wake test successfully booted PECD-NK.
Service installation still needs the Pi's sudo password. Do not cut Pi power
while Linux is running. Linux poweroff
does not remove all board power; this arrangement wakes from halt rather than
switching the Pi's external supply.

Daily collection retrieves the latest retained logs, **not a guaranteed full
24-hour history**. With one-second sampling and the current pair of 256 KiB log
files, older records rotate out within a day. Complete daily history needs a
separate logging-rate/storage-capacity change.

References: [Pi hardware](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html),
[pyserial RTS/DTR](https://pyserial.readthedocs.io/en/latest/pyserial_api.html),
[systemd lifecycle](https://www.freedesktop.org/software/systemd/man/262/systemd.service.html).

## Verification

```bash
python3 -m unittest discover -s Tests/computer_vision -p 'test_*.py' -v
```

Eleven tests cover logs-only mode, routing, multi-region alarms, framing, incomplete exports,
timeouts, path rejection, beep acknowledgement and log collection after
vision/alarm failures.

On the Pi, healthy pepper skipped the large model (~6.7 s small-model time).
Bacterial-spot pepper used the large model and returned unhealthy (~6.0 s
small + 1.3 s large). A live camera capture and inference also completed,
reporting uncertain with species unknown. These checks establish operation,
not field accuracy or energy savings.

The UART link subsequently passed a full capture/log cycle and acknowledged
`BEEP 4`. A logs-only run saved about 65 KiB in
`~/plant-monitor/runs/20261119T150512Z-0a702c97/`, with no camera or model use.
The daily-wake firmware builds with ESP-IDF 6.1. Host C tests cover the daily
deadline, 64-bit timing, missed deadlines without repeated pulses, and returning
to a daily interval after an accelerated test:

```bash
cc -std=c11 -Wall -Wextra -Werror Tests/computer_vision/test_pi_wake_schedule.c -o /tmp/plant-wake-test
/tmp/plant-wake-test
```

The daily-wake firmware was flashed and verified on 2026-11-19, preserving the
SPIFFS partition. `WAKE_IN 60` produced a logged GPIO25 pulse after 60.419 seconds
and restored the 86400-second interval. The user observed boot activity; SSH
returned with a new boot ID and about 66 seconds of uptime. Evidence is in
`Results/pi_wake_test_2026-11-19.json` in the repository. The boot
service's actual automatic shutdown remains untested until installation with
sudo is completed. A post-wake manual cycle captured and analysed a photo, but
its UART handshake timed out. Subsequent 115200-baud probes received no bytes;
the ESP32 USB console still showed normal sensor sampling. Verify the physical
UART2 connections (GPIO16/RX2 and GPIO17/TX2, not RX0/TX0) before enabling the
service. The failed run is `~/plant-monitor/runs/20261119T151941Z-09fea976/`.
