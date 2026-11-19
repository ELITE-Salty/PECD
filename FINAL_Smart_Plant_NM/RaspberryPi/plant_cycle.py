"""One capture / inference / ESP32 log collection cycle. Never powers off by itself."""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid


def save_json(path, value):
    temporary = path.with_suffix(".tmp")
    with temporary.open("w") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def load_module(name, directory):
    directory = Path(directory).expanduser().resolve()
    spec = importlib.util.spec_from_file_location(name, directory / "main.py")
    module = importlib.util.module_from_spec(spec)
    # The small model imports its supplied helpers.py.
    sys.path.insert(0, str(directory))
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    return module


def colour_decision(regions, policy):
    """An inexpensive screening heuristic, not a disease diagnosis."""
    if not regions:
        return "unknown"
    if all(r["green"] >= policy["healthy_green_min"] and
           r["yellow"] + r["brown"] <= policy["healthy_damage_max"]
           for r in regions):
        return "healthy-ish"
    if any(r["green"] <= policy["stress_green_max"] and
           r["yellow"] + r["brown"] >= policy["stress_damage_min"]
           for r in regions):
        return "stressed-ish"
    return "uncertain"


def analyse(image_path, config, output):
    import cv2
    import numpy as np

    start = time.monotonic()
    small = load_module("plant_small", config["small_model_dir"])
    image = cv2.imread(str(image_path))
    if image is None:
        raise ValueError("Camera image could not be decoded")
    h, w = image.shape[:2]
    if max(h, w) > 1200:
        scale = 1200 / max(h, w)
        image = cv2.resize(image, (round(w * scale), round(h * scale)))
    session, name = small.load_model()
    saliency = small.infer_map(session, name, image)
    if not np.isfinite(saliency).all():
        raise ValueError("Small model returned non-finite values")
    masks = small.make_leaf_masks(saliency, image)
    del session  # Avoid keeping both models resident on the 1 GB Pi.
    regions = [{k: float(v) for k, v in small.color_fracs(image, m).items()}
               for m in masks]
    decision = colour_decision(regions, config["policy"])
    result = {"status": decision, "alert": decision == "stressed-ish",
              "regions": regions, "large_model_used": False,
              "small_seconds": round(time.monotonic() - start, 3)}
    overlay, indices = small.draw_overlay(image, masks, [
        f"Region {i}: G={r['green']:.2f} Y={r['yellow']:.2f} B={r['brown']:.2f}"
        for i, r in enumerate(regions, 1)])
    if not cv2.imwrite(str(output / "overlay.jpg"), overlay):
        raise OSError("Could not save overlay")
    if not cv2.imwrite(str(output / "regions.png"), indices):
        raise OSError("Could not save region mask")
    if not masks:
        result["reason"] = "No foreground region; do not classify an empty scene as healthy"
        return result
    if decision != "uncertain":
        return result
    # Species must be supplied by the operator; confidence cannot establish it.
    if config["plant_species"] not in {"pepper", "potato", "tomato"}:
        result["reason"] = "Large classifier requires a confirmed supported plant species"
        return result
    if len(masks) > config["policy"]["max_regions"]:
        result["reason"] = "Too many foreground regions for a bounded cycle"
        return result
    large = load_module("plant_large", config["large_model_dir"])
    start = time.monotonic()
    session, labels = large.load_model()
    predictions = []
    for i, mask in enumerate(masks, 1):
        # Classifier was trained on individual leaves, not entire plant scenes.
        x, y, w, h = cv2.boundingRect(mask)
        margin = max(4, round(max(w, h) * 0.08))
        crop = image[max(0, y-margin):y+h+margin, max(0, x-margin):x+w+margin]
        crop_path = output / f"region-{i}.jpg"
        if not cv2.imwrite(str(crop_path), crop):
            raise OSError("Could not save classifier crop")
        condition, healthy, unhealthy = large.classify_image(session, labels, crop_path)
        if not np.isfinite([healthy, unhealthy]).all():
            raise ValueError("Classifier returned non-finite scores")
        predictions.append({"region": i, "condition": condition,
                            "healthy": healthy, "unhealthy": unhealthy})
    threshold = config["policy"]["classifier_confidence_min"]
    alert = any(p["unhealthy"] >= threshold for p in predictions)
    healthy = all(p["healthy"] >= threshold for p in predictions)
    result.update(large_model_used=True, predictions=predictions, alert=alert,
                  status="unhealthy" if alert else "healthy" if healthy else "uncertain",
                  large_seconds=round(time.monotonic() - start, 3))
    return result


class EspLink:
    def __init__(self, port, timeout=90, transport=None):
        if transport is None:
            import serial
            transport = serial.Serial(port=None, baudrate=115200, timeout=0.25,
                                      write_timeout=3, exclusive=True)
            transport.dtr = False
            transport.rts = False
            transport.port = port
            transport.open()
        self.serial = transport
        self.timeout = timeout
        self.pending = bytearray()

    def close(self):
        self.serial.close()

    def send(self, command):
        self.serial.write((command + "\n").encode("ascii"))

    def lines(self, timeout=None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            chunk = self.serial.read(1)
            if not chunk:
                continue
            self.pending.extend(chunk)
            if len(self.pending) > 2048:
                raise ValueError("Oversized serial frame")
            if chunk == b"\n":
                line = self.pending.decode("utf-8", errors="strict").strip()
                self.pending.clear()
                if line == "PING":  # Compatible with the lab wake handshake.
                    self.send("READY")
                else:
                    yield line
        raise TimeoutError("ESP32 response timed out")

    def ping(self):
        self.send("PING")
        for line in self.lines():
            if line == "@@PLANT|READY":
                return
            if line.startswith("@@PLANT|ERROR|"):
                raise RuntimeError(line)

    def beep(self, count):
        if not 1 <= count <= 10:
            raise ValueError("Beep count must be 1..10")
        self.send(f"BEEP {count}")
        for line in self.lines():
            if line == f"@@PLANT|BEEP|{count}|DONE":
                return
            if line.startswith("@@PLANT|ERROR|"):
                raise RuntimeError(line)

    def logs(self, output):
        expected = {"events_prev.txt", "events_log.txt"}
        completed = set()
        active = None
        stream = None
        self.send("logs")
        try:
            for line in self.lines():
                if not line.startswith("@@PLANT|"):
                    continue  # UART0 may also contain ESP-IDF console output.
                parts = line.split("|", 3)
                kind = parts[1]
                if kind == "ERROR":
                    raise RuntimeError(line)
                if kind == "DONE":
                    if active or completed != expected:
                        raise ValueError("Incomplete ESP32 log export")
                    if not (output / "events_log.txt").exists():
                        raise ValueError("ESP32 has no current log file")
                    return sorted(completed)
                if kind not in {"BEGIN", "LINE", "END", "MISSING"}:
                    continue
                if len(parts) < 3 or parts[2] not in expected:
                    raise ValueError("Unexpected log filename")
                name = parts[2]
                if kind == "BEGIN":
                    if active or name in completed:
                        raise ValueError("Unexpected log BEGIN")
                    active = name
                    stream = (output / (name + ".partial")).open("w")
                elif kind == "LINE":
                    if active != name or len(parts) != 4:
                        raise ValueError("Unexpected log LINE")
                    stream.write(parts[3] + "\n")
                elif kind == "END":
                    if active != name:
                        raise ValueError("Unexpected log END")
                    stream.flush()
                    os.fsync(stream.fileno())
                    stream.close()
                    stream = None
                    (output / (name + ".partial")).replace(output / name)
                    active = None
                    completed.add(name)
                elif kind == "MISSING":
                    if active or name in completed:
                        raise ValueError("Unexpected log MISSING")
                    completed.add(name)
        finally:
            if stream is not None:
                stream.close()  # Keep .partial for diagnosis; never mark it complete.


def capture(path, config):
    subprocess.run(["rpicam-still", "--nopreview", "--timeout", "2000",
                    "--width", str(config["width"]), "--height", str(config["height"]),
                    "--output", str(path)], check=True, timeout=25)


def run_cycle(config, source=None, offline=False, logs_only=False):
    if logs_only and (source is not None or offline):
        raise ValueError("Logs-only mode cannot be combined with image/offline mode")
    started = datetime.now(timezone.utc).isoformat()
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    output = Path(config["output_dir"]).expanduser() / run_id
    output.mkdir(parents=True, exist_ok=False)
    report = {"started_utc": started, "run_id": run_id, "errors": [],
              "serial": "skipped" if offline else "pending", "alarm": "not_requested"}
    save_json(output / "config.json", config)
    # Collect sensor logs even if the camera or models fail.
    if logs_only:
        report["vision"] = {"status": "skipped", "reason": "logs-only mode", "alert": False}
    else:
        try:
            image_path = output / "capture.jpg"
            if source:
                shutil.copyfile(source, image_path)
            else:
                capture(image_path, config["camera"])
            report["vision"] = analyse(image_path, config, output)
        except Exception as exc:
            report["errors"].append(f"vision: {type(exc).__name__}: {exc}")
    save_json(output / "result.json", report)
    if not offline:
        link = None
        try:
            link = EspLink(config["serial_port"], config["serial_timeout_seconds"])
            link.ping()
            if report.get("vision", {}).get("alert"):
                # No blind retry: a lost acknowledgement must not replay the alarm.
                try:
                    link.beep(config["alarm_beeps"])
                    report["alarm"] = "acknowledged"
                except Exception as exc:
                    report["alarm"] = "unconfirmed"
                    report["errors"].append(f"alarm: {type(exc).__name__}: {exc}")
            report["log_files"] = link.logs(output)
            report["serial"] = "complete"
        except Exception as exc:
            report["serial"] = "failed"
            report["errors"].append(f"serial: {type(exc).__name__}: {exc}")
        finally:
            if link is not None:
                link.close()
    report["finished_utc"] = datetime.now(timezone.utc).isoformat()
    save_json(output / "result.json", report)
    print(json.dumps({"output": str(output), **report}, indent=2))
    return 1 if report["errors"] else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path(__file__).with_name("config.json"))
    parser.add_argument("--image", type=Path, help="Use an existing photo instead of the camera")
    parser.add_argument("--offline", action="store_true", help="Skip all ESP32 communication")
    parser.add_argument("--logs-only", action="store_true", help="Collect ESP32 logs without camera, models or alarms")
    args = parser.parse_args()
    if args.logs_only and (args.image is not None or args.offline):
        parser.error("--logs-only cannot be combined with --image or --offline")
    config = json.loads(args.config.read_text())
    return run_cycle(config, args.image, args.offline, args.logs_only)


if __name__ == "__main__":
    sys.exit(main())
