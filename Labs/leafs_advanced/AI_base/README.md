# Leafs: classify a leaf photograph

Programming Energy-Constrained Devices, FRI. Basic Python is required; prior AI and NumPy knowledge are not assumed. The handout includes a short introduction, worked numeric examples, and self-check answers with common fixes. Follow **lab.pdf** to build one short `main.py`: load the supplied model, prepare pixels, run inference, interpret the scores, and repeat for the photographs in `images/`.

## One setup for Pi 3 and Pi 4

Use **Raspberry Pi OS Bookworm (64-bit) and Python 3.11** on Pi 3B/3B+ and Pi 4B. The same code, requirements, and commands apply to both. The instructor prepares the OS before class; in Raspberry Pi Imager the Bookworm image is currently labelled **Raspberry Pi OS (Legacy, 64-bit)**. See [official image downloads](https://www.raspberrypi.com/software/operating-systems/).

From the folder containing `main.py`, check the environment, create `.venv` once, and activate it:

```bash
dpkg --print-architecture
python3.11 --version
python3.11 -m venv .venv
source .venv/bin/activate
python -c "import sys, struct; print(sys.executable); print(struct.calcsize('P') * 8)"
python -m pip install --only-binary=:all: -r requirements.txt
python -c "import numpy, PIL, onnxruntime; print(onnxruntime.__version__)"
```

Expected: `arm64`, Python `3.11.x`, an interpreter inside `.venv`, `64` bits, and runtime `1.18.1`. The instructor installs `python3-venv` and `python3-pip` if missing. `--only-binary=:all:` uses prebuilt packages and stops if this environment has no compatible wheel. Ask the instructor to check the image/Python version if installation fails.

Activate `.venv` in every new terminal; keep your source code outside it. Use `python -m pip`, never `sudo pip`. You do not recreate the venv or reinstall packages each session. The instructor must still test the installation and sample predictions on both real boards before class.

## Run your completed script

```bash
source .venv/bin/activate
python main.py
# When finished:
deactivate
```

In a new terminal, return to this folder and activate again. You do not recreate the venv or reinstall packages each time. The starter intentionally contains only imports and TODO comments until you complete the handout.

Expected results: healthy pepper and potato photographs are **healthy**; bacterial-spot and early-blight examples are **unhealthy**. The group scores sum to approximately one. Assets are found relative to `main.py`, allowing execution from another directory.

Use close-up RGB JPEG/PNG photographs of individual bell pepper, potato, or tomato leaves. Unsupported species and non-leaves can produce confident mistakes. "Unhealthy" covers the model's disease classes, not every plant problem. Transparent images are outside this baseline; corrupt files produce normal Python errors.

Prediction timing, camera capture, CSV/history, and model replacement are optional extensions. Everyone discusses a suitable plant-monitoring interval; no timer is needed for that discussion. No network is needed for inference. See [THIRD_PARTY.md](THIRD_PARTY.md) and [ASSETS.json](ASSETS.json) for licenses, attribution, source revisions, and hashes.

## Required experiments

Use the results worksheet on the last PDF page (print it or reproduce it in your notes). Write predictions before reading the answers. Every classification checkpoint uses `python main.py` with a temporary main body and diagnostic prints. Complete the normalization exercise and restore the final baseline script. Timing is optional; if you try it, restore the baseline afterwards.

For the final check, classify **two additional photographs**: a new camera photo of a supported leaf and an unsuitable image (such as a non-leaf or unsupported species). Bring your own or use independent photos supplied by the instructor, not another PlantVillage example. Record your expectation before running, the condition and both scores, and a short explanation of the result. Submit the filled worksheet with your code and the four sample predictions. The worksheet lists the five completion criteria. Model improvement and camera integration remain optional.
