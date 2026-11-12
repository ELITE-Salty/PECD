# Leaf monitoring - original model starter

Start with **lab.pdf**. Fill the TODO functions in `main.py` progressively; run `python main.py` at each checkpoint. The initial `NotImplementedError` is intentional. `helpers.py` supplies image loading, connected regions, color rules, and drawing; it contains no model inference pipeline.

The original U2-Net model selects foreground. Hand-written color rules then report `healthy-ish`, `stressed-ish`, or `borderline`; these are rough indicators, not disease diagnoses. There is no softmax or class-label file for this model.

## Setup before class

Use Raspberry Pi OS Bookworm 64-bit with Python 3.11. The instructor prepares `python3-venv`, `python3-pip`, `libgl1`, and `libglib2.0-0` first. From this folder:

```bash
python3.11 -m venv .venv
source .venv/bin/activate
python -m pip install --only-binary=:all: -r requirements.txt
python -c "import cv2, numpy, onnxruntime; print(cv2.__version__)"
```

Activate the venv in every new terminal. Full setup and architecture checks are in the PDF. Pi 3/4 execution still needs classroom hardware verification.

## Photographs and output

Two pepper photographs are supplied under `images/`. The default is `images/pepper_healthy.jpg`; change `IMAGE_FILE` to test the other or your own photo. Dataset labels do not guarantee agreement with the color rules. The completed script writes `leaf_overlay.jpg` and the numbered `leaf_masks.png` beside `main.py`.

Images: Mohanty, Hughes and Salathe / PlantVillage, CC BY-SA 3.0; enlarged to 800 pixels wide for readable overlays. See `ASSETS.json` for pinned sources, hashes, model provenance, and changes, and `LICENSE-IMAGES.txt` for the image license. The illustrated handout uses the same credited source.
