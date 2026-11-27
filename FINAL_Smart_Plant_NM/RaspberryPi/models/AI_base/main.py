"""Classify individual leaf photographs with the supplied ONNX model."""
# STEP 0: Imports and the fixed preprocessing settings for this model.
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image, ImageOps

BASE = Path(__file__).resolve().parent
RESIZE_SIZE = 256
CROP_SIZE = 224
MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD = np.array([0.229, 0.224, 0.225], dtype=np.float32)
TEMPERATURE = 1.210394024848938

# STEP 1: Load the model and its ordered labels once.
def load_model():
    session = ort.InferenceSession(
    str(BASE / "model.onnx"), providers=["CPUExecutionProvider"]
    )
    labels = json.loads((BASE / "labels.json").read_text())
    return session, labels

# STEP 2: Convert a photograph to the tensor expected by the model.
def prepare_image(image_path):
    with Image.open(image_path) as source:
        image = ImageOps.exif_transpose(source)
        image = image.convert("RGB")
    width, height = image.size
    long_side = int(RESIZE_SIZE * max(width, height) / min(width, height))
    size = (RESIZE_SIZE, long_side) if width <= height else (long_side, RESIZE_SIZE)
    image = image.resize(size, Image.Resampling.BILINEAR)
    left = round((image.width - CROP_SIZE) / 2)
    top = round((image.height - CROP_SIZE) / 2)
    image = image.crop((left, top, left + CROP_SIZE, top + CROP_SIZE))
    pixels = np.asarray(image, dtype=np.float32) / np.float32(255.0)
    pixels = (pixels - MEAN) / STD
    # HWC -> CHW; add N so the shape is (1, 3, 224, 224).
    channels_first = pixels.transpose(2, 0, 1)
    batch = np.expand_dims(channels_first, axis=0)
    #print("pixels:", pixels.shape, pixels.dtype)
    #print("channels_first:", channels_first.shape, channels_first.dtype)
    #print("batch:", batch.shape, batch.dtype)
    return np.ascontiguousarray(batch)

# STEP 3: Run inference. The model returns 15 raw scores (logits).
def classify_image(session, labels, image_path):
    tensor = prepare_image(image_path)
    input_name = session.get_inputs()[0].name
    outputs = session.run(None, {input_name: tensor})
    output_tensor = outputs[0]
    logits = output_tensor[0]
    #print("Outputs:", type(outputs))
    #print("Tensor and row shapes:", output_tensor.shape, logits.shape)
    #print("Logits:", logits)
    # STEP 4: Softmax, then sum the healthy and disease class probabilities.
    scores = logits.astype(np.float64) / TEMPERATURE
    scores = scores - scores.max() # Avoid overflow in exp().
    weights = np.exp(scores)
    probabilities = weights / weights.sum()
    healthy = np.array([name.lower().endswith("healthy") for name in labels])
    healthy_score = float(probabilities[healthy].sum())
    unhealthy_score = float(probabilities[~healthy].sum())
    condition = "healthy" if healthy_score > unhealthy_score else "unhealthy"
    return condition, healthy_score, unhealthy_score

# STEP 5: Repeat for the photographs in images/ and print the results.
def main():
    session, labels = load_model()
    for image_path in sorted((BASE / "images").iterdir()):
        if not image_path.is_file() or image_path.suffix.lower() not in {".jpg", ".jpeg", ".png"}:
            continue
        condition, healthy_score, unhealthy_score = classify_image(
            session, labels, image_path
        )
        print(f"{image_path.name}: {condition} | "
            f"healthy={healthy_score:.3f} unhealthy={unhealthy_score:.3f}")

'''def main():
    from time import perf_counter
    session, labels = load_model()
    path = BASE / "images/pepper_healthy.jpg"
    start = perf_counter()
    result = classify_image(session, labels, path)
    prediction_ms = (perf_counter() - start) * 1000
    print(f"Prediction time: {prediction_ms:.3f} ms")
    print("Result:", result)
'''
if __name__ == "__main__":
    main()
