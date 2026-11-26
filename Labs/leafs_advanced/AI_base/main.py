"""Follow lab.pdf to build a leaf classifier in this file."""
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image, ImageOps

# 1. Load model.onnx and the ordered labels from labels.json once.
# 2. Prepare an image: RGB, resize, crop, normalize, and change tensor layout.
# 3. Run session.run() and inspect the raw output scores.
# 4. Use softmax and group the scores into healthy and unhealthy.
# 5. Repeat for images/ and print the results.
