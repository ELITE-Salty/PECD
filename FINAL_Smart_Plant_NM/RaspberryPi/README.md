# Raspberry Pi

**Status: in development by the team member responsible for the Raspberry Pi.
No Raspberry Pi code has been integrated into this repository yet.** This
file records the intended responsibilities and the material available so far;
it does not describe an existing implementation.

## Intended responsibilities

- Raspberry Pi setup (OS, Python environment).
- Communication with the ESP32 (protocol not yet decided).
- RGB camera acquisition.
- Computer vision for visual plant distress (e.g. leaf yellowing, leaf
  count), executed on-device with resource-efficient models.
- System-level coordination, depending on the final architecture.

## Material available from the course labs (not yet adopted)

These lab starters exist in the team's working directory. They are templates
with `TODO`s, not completed implementations.

| Lab folder | Approach | Model | Environment stated by the lab |
| --- | --- | --- | --- |
| `leafs-starter/` | U2-Net-p foreground segmentation + hand-written colour rules (healthy-ish / stressed-ish) | `u2netp.onnx` (~4.6 MB) | Pi OS Bookworm 64-bit, Python 3.11, onnxruntime, OpenCV |
| `leafs_advanced/AI_base/` | 15-class PlantVillage leaf classifier (pepper/potato/tomato), grouped into healthy/unhealthy | `model.onnx` (~17 MB) | Pi OS Bookworm 64-bit, Python 3.11, onnxruntime 1.18.1 |
| `pi_wake/main/Something.py` | UART line protocol prototype (`PING`/`READY`/`ACK`) on `/dev/serial0`, 115200 baud | - | pyserial |

Note: the classifier in `leafs_advanced` only covers pepper, potato and
tomato. Whether it is suitable depends on the species grown from the
greenhouse kit (to be confirmed).

## To be documented here by the Raspberry Pi owner

- Raspberry Pi model, OS version, camera model and interface.
- Setup and execution instructions.
- Chosen model(s), source, licence, size, quantisation, and SHA-256 hash
  (model files are excluded from git by `.gitignore`).
- Communication protocol with the ESP32.
