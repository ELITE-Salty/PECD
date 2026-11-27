"""Exercise using the original leafs model and helper functions."""
from pathlib import Path
import cv2
import numpy as np
import onnxruntime as ort
from helpers import (read_and_resize, split_leaf_masks, color_fracs,
                     health_from_colors, draw_overlay)

BASE = Path(__file__).resolve().parent
IMAGE_FILE = "images/pepper_bacterial_spot.jpg"

# Checkpoint 1: load the supplied model once.
def load_model():
    session = ort.InferenceSession(
        str(BASE / "u2netp.onnx"), providers=["CPUExecutionProvider"]
    )
    input_name = session.get_inputs()[0].name
    return session, input_name


# Checkpoint 2: prepare the model's input tensor.
def prepare_image(image):
    rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    resized = cv2.resize(rgb, (320, 320), interpolation=cv2.INTER_AREA)
    pixels = resized.astype(np.float32) / 255.0
    channels_first = pixels.transpose(2, 0, 1)
    batch = np.expand_dims(channels_first, axis=0)
    return np.ascontiguousarray(batch)


# Checkpoint 3: run the learned foreground detector.
def infer_map(session, input_name, image):
    batch = prepare_image(image)
    outputs = session.run(None, {input_name: batch})
    primary = outputs[0]
    saliency = primary[0, 0]
    return saliency


# Checkpoint 4: turn saliency into separate foreground regions.
def make_leaf_masks(saliency, image):
    low, high = saliency.min(), saliency.max()
    scaled = 255 * (saliency - low) / (high - low + 1e-8)
    scaled = scaled.astype(np.uint8)
    height, width = image.shape[:2]
    full_size = cv2.resize(scaled, (width, height),
            interpolation=cv2.INTER_LINEAR)
    _, binary = cv2.threshold(full_size, 0, 255,
            cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    kernel = np.ones((5, 5), dtype=np.uint8)
    clean = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, kernel, iterations=2)
    clean = cv2.morphologyEx(clean, cv2.MORPH_OPEN, kernel, iterations=1)
    return split_leaf_masks(clean)


def main():
    session, input_name = load_model()
    image = read_and_resize(str(BASE / IMAGE_FILE))
    saliency = infer_map(session, input_name, image)
    masks = make_leaf_masks(saliency, image)
    lines = []
    if not masks:
        print("No leaf-like regions detected.")
    for number, mask in enumerate(masks, 1):
        fractions = color_fracs(image, mask)
        condition = health_from_colors(fractions)
        text = (f"Leaf {number}: G={fractions['green']:.2f} "
            f"Y={fractions['yellow']:.2f} B={fractions['brown']:.2f} "
            f"{condition}")
        print(text)
        lines.append(text)
    overlay, index_map = draw_overlay(image, masks, lines)
    cv2.imwrite(str(BASE / "leaf_overlay.jpg"), overlay)
    cv2.imwrite(str(BASE / "leaf_masks.png"), index_map)

if __name__ == "__main__":
    main()
