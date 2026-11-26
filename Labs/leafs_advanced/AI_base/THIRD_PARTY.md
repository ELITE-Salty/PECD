# Supplied model and photograph notices

## Model

MobileNetV3-Large leaf classifier by Hugging Face user **sarang-sketch**, published as **Jamee Crop Disease Classifier / sarox-plangton**.

- Source: https://huggingface.co/sarang-sketch/sarox-plangton
- Revision: `039a76b89e7caa1cf315b9637aaaa64bf238fbe5`
- Publisher license declaration: Apache-2.0. License text: `LICENSE-MODEL.txt`.
- The model bytes are unchanged. This lab uses simplified inference code, fixed preprocessing constants, temperature-scaled softmax, and healthy/unhealthy grouping instead of the publisher's top-k/uncertainty API.
- Ordered class names are copied from the same revision. Model and image checksums are recorded in `ASSETS.json`.

The model was trained on PlantVillage crop/disease images. It is not a general plant-health diagnostic. Publisher metrics are not independently verified by this lab.

## Photographs

PlantVillage dataset by **Sharada P. Mohanty, David P. Hughes, and Marcel Salathe** (2016), Digital Epidemiology Lab, EPFL.

- Source: https://github.com/spMohanty/PlantVillage-Dataset
- Revision: `7f7ecc7e1eaca78107e3affe7cb5abd9427e139a`
- Dataset publisher declares CC BY-SA 3.0: https://huggingface.co/datasets/mohanty/PlantVillage
- Terms: https://creativecommons.org/licenses/by-sa/3.0/ and `images/LICENSE-CC-BY-SA-3.0.txt`.
- Exact image source URLs, authorship attribution, and hashes: `ASSETS.json`.
- Only filenames changed; image bytes were not edited. Retain attribution and the license when sharing; adaptations of these photographs remain subject to the ShareAlike terms.

Paper: *Using Deep Learning for Image-Based Plant Disease Detection*, Mohanty, Hughes and Salathe (2016), DOI https://doi.org/10.3389/fpls.2016.01419.

The preprocessing illustration in `lab.pdf` adapts `pepper_healthy.jpg` by enlarging, padding, resizing, and cropping it. That illustration is shared under CC BY-SA 3.0 with the same PlantVillage attribution. The bundled source photograph remains unchanged.
