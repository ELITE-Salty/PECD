"""Protocol and orchestration checks; no camera, models or serial hardware needed."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("plant_cycle", ROOT / "RaspberryPi/plant_cycle.py")
cycle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cycle)
CONFIG = json.loads((ROOT / "RaspberryPi/config.json").read_text())


class Wire:
    def __init__(self, text):
        self.data = bytearray(text.encode())
        self.writes = []

    def read(self, count):
        if not self.data:
            return b""
        result = bytes(self.data[:count])
        del self.data[:count]
        return result

    def write(self, data):
        self.writes.append(data)

    def close(self):
        pass


class CycleTests(unittest.TestCase):
    def test_logs_only_skips_camera_models_and_alarm(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = {**CONFIG, "output_dir": tmp}
            with patch.object(cycle, "capture") as capture, \
                    patch.object(cycle, "analyse") as analyse, \
                    patch.object(cycle, "EspLink") as link, patch("builtins.print"):
                link.return_value.logs.return_value = ["events_log.txt"]
                self.assertEqual(cycle.run_cycle(config, logs_only=True), 0)
                capture.assert_not_called()
                analyse.assert_not_called()
                link.return_value.beep.assert_not_called()
                link.return_value.ping.assert_called_once()
                link.return_value.logs.assert_called_once()
                report = json.loads(next(Path(tmp).glob("*/result.json")).read_text())
                self.assertEqual(report["vision"]["status"], "skipped")
                self.assertEqual(report["serial"], "complete")

    def test_logs_only_rejects_conflicting_modes(self):
        with self.assertRaises(ValueError):
            cycle.run_cycle(CONFIG, offline=True, logs_only=True)
        with self.assertRaises(ValueError):
            cycle.run_cycle(CONFIG, source="image.jpg", logs_only=True)

    def test_colour_gate_does_not_clear_bacterial_spot_sample(self):
        policy = CONFIG["policy"]
        self.assertEqual(cycle.colour_decision([
            {"green": .747, "yellow": .114, "brown": .140}], policy), "uncertain")
        self.assertEqual(cycle.colour_decision([
            {"green": .985, "yellow": .0005, "brown": .0001}], policy), "healthy-ish")
        self.assertEqual(cycle.colour_decision([], policy), "unknown")

    def test_one_damaged_region_triggers_alert_path(self):
        self.assertEqual(cycle.colour_decision([
            {"green": .99, "yellow": 0, "brown": 0},
            {"green": .2, "yellow": .6, "brown": .1}], CONFIG["policy"]), "stressed-ish")

    def test_logs_accept_missing_previous_and_preserve_content(self):
        wire = Wire("boot noise\n@@PLANT|MISSING|events_prev.txt\n"
                    "@@PLANT|BEGIN|events_log.txt\n"
                    "@@PLANT|LINE|events_log.txt|uptime=5s: value|detail\n"
                    "@@PLANT|END|events_log.txt\n@@PLANT|DONE\n")
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            cycle.EspLink(None, transport=wire).logs(output)
            self.assertEqual((output / "events_log.txt").read_text(), "uptime=5s: value|detail\n")
            self.assertFalse(list(output.glob("*.partial")))
        self.assertEqual(wire.writes, [b"logs\n"])

    def test_partial_export_never_looks_complete(self):
        wire = Wire("@@PLANT|BEGIN|events_log.txt\n"
                    "@@PLANT|LINE|events_log.txt|one line\n@@PLANT|DONE\n")
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                cycle.EspLink(None, transport=wire).logs(Path(tmp))
            self.assertTrue((Path(tmp) / "events_log.txt.partial").exists())
            self.assertFalse((Path(tmp) / "events_log.txt").exists())

    def test_storage_error_and_timeout_are_failures(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(RuntimeError):
                cycle.EspLink(None, transport=Wire("@@PLANT|ERROR|SPIFFS not mounted\n")).logs(Path(tmp))
            with self.assertRaises(TimeoutError):
                cycle.EspLink(None, timeout=.01, transport=Wire("")).logs(Path(tmp))

    def test_beep_requires_matching_completion(self):
        wire = Wire("@@PLANT|BEEP|2|DONE\n@@PLANT|BEEP|4|DONE\n")
        cycle.EspLink(None, transport=wire).beep(4)
        self.assertEqual(wire.writes, [b"BEEP 4\n"])
        with self.assertRaises(TimeoutError):
            cycle.EspLink(None, timeout=.01, transport=Wire("")).beep(4)

    def test_filename_cannot_escape_run_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                cycle.EspLink(None, transport=Wire("@@PLANT|BEGIN|../bad\n")).logs(Path(tmp))

    def test_camera_failure_still_collects_logs(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = {**CONFIG, "output_dir": tmp}
            with patch.object(cycle, "capture", side_effect=RuntimeError("no camera")), \
                    patch.object(cycle, "EspLink") as link, patch("builtins.print"):
                link.return_value.logs.return_value = ["events_log.txt"]
                self.assertEqual(cycle.run_cycle(config), 1)
                link.return_value.logs.assert_called_once()
                link.return_value.beep.assert_not_called()
                report = json.loads(next(Path(tmp).glob("*/result.json")).read_text())
                self.assertEqual(report["serial"], "complete")
                self.assertIn("no camera", report["errors"][0])

    def test_alarm_ack_failure_still_collects_logs_without_replaying_alarm(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = {**CONFIG, "output_dir": tmp}
            with patch.object(cycle, "capture"), \
                    patch.object(cycle, "analyse", return_value={"alert": True}), \
                    patch.object(cycle, "EspLink") as link, patch("builtins.print"):
                link.return_value.beep.side_effect = TimeoutError("lost ack")
                link.return_value.logs.return_value = ["events_log.txt"]
                self.assertEqual(cycle.run_cycle(config), 1)
                link.return_value.beep.assert_called_once_with(4)
                link.return_value.logs.assert_called_once()


if __name__ == "__main__":
    unittest.main()
