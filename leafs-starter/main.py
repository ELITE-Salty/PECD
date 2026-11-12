"""Exercise using the original leafs model and helper functions."""
from pathlib import Path
import cv2
import numpy as np
import onnxruntime as ort
from helpers import (read_and_resize, split_leaf_masks, color_fracs,
                     health_from_colors, draw_overlay)

BASE = Path(__file__).resolve().parent
IMAGE_FILE = "images/pepper_healthy.jpg"

# Checkpoint 1: load the supplied model once.
def load_model():
    """Return the CPU inference session and its input name."""
    # TODO: create an InferenceSession for BASE / "u2netp.onnx".
    # Read its input name and return (session, input_name).
    raise NotImplementedError("Checkpoint 1: load_model")


# Checkpoint 2: prepare the model's input tensor.
def prepare_image(image):
    """Convert a BGR image to float32 RGB with shape (1, 3, 320, 320)."""
    # TODO: BGR -> RGB, resize to 320 x 320, convert to float32, / 255.
    # Change HWC -> CHW, add the batch axis, return a contiguous array.
    raise NotImplementedError("Checkpoint 2: prepare_image")


# Checkpoint 3: run the learned foreground detector.
def infer_map(session, input_name, image):
    """Return the first output's 320 x 320 saliency map."""
    # TODO: prepare the image, call session.run, select output 0 and [0, 0].
    raise NotImplementedError("Checkpoint 3: infer_map")


# Checkpoint 4: turn saliency into separate foreground regions.
def make_leaf_masks(saliency, image):
    """Return uint8 masks at the working image size, using 0 and 255."""
    # TODO: min-max scale to uint8, restore image size, use Otsu threshold.
    # Close twice, open once with a 5 x 5 kernel, then split_leaf_masks.
    raise NotImplementedError("Checkpoint 4: make_leaf_masks")


def main():
    """Follow each temporary main in the PDF; finish at checkpoint 6."""
    # TODO: replace this function with each checkpoint's temporary main.
    # Finally combine the pipeline, inspect color fractions, print and save.
    raise NotImplementedError("Start at checkpoint 1 in lab.pdf")


if __name__ == "__main__":
    main()
