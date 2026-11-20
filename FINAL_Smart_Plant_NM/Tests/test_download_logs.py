"""USB download checks without hardware: python -m unittest discover -s ../Tests -p test_download_logs.py"""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


SOURCE = Path(__file__).resolve().parents[1] / "ESP32/tools/download_logs.py"
spec = importlib.util.spec_from_file_location("download_logs", SOURCE)
download = importlib.util.module_from_spec(spec)
spec.loader.exec_module(download)


class Port:
    def __init__(self, chunks):
        self.chunks = iter(chunks)
        self.writes = []

    def write(self, data):
        self.writes.append(data)

    def read(self, size):
        return next(self.chunks, b"")


class DownloadTests(unittest.TestCase):
    def test_fragmented_transfer_separates_files_and_skips_console(self):
        wire = (b"I (20) moisture: Raw=2310\r\n"
                b"--- /spiffs/events_prev.txt ---\r\n"
                b"uptime=1s: old reading\r\n"
                b"--- /spiffs/events_log.txt ---\r\n"
                b"uptime=2s: air_temperature = 23.6\r\n"
                b"--- End of saved logs ---\r\nI (30) moisture: Raw=2311\r\n")
        port = Port([wire[i:i+7] for i in range(0, len(wire), 7)])
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            files = download.receive_logs(port, output)
            self.assertEqual([p.name for p in files], ["events_prev.txt", "events_log.txt"])
            self.assertEqual(files[1].read_bytes(), b"uptime=2s: air_temperature = 23.6\n")
            self.assertEqual(files[0].read_bytes(), b"uptime=1s: old reading\n")
            self.assertEqual(list(output.glob("*.partial")), [])
        self.assertEqual(port.writes, [b"logs\n"])

    def test_current_log_without_previous(self):
        port = Port([b"--- /spiffs/events_log.txt ---\n--- End of saved logs ---\n"])
        with tempfile.TemporaryDirectory() as tmp:
            files = download.receive_logs(port, Path(tmp))
            self.assertEqual([p.name for p in files], ["events_log.txt"])

    def test_interruption_never_publishes_incomplete_file(self):
        port = Port([b"--- /spiffs/events_log.txt ---\nuptime=2s: value\n"])
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            with patch.object(download.time, "monotonic", side_effect=[0, 0, 181]):
                with self.assertRaises(TimeoutError):
                    download.receive_logs(port, output)
            self.assertFalse((output / "events_log.txt").exists())
            self.assertEqual((output / "events_log.txt.partial").read_bytes(), b"uptime=2s: value\n")

    def test_storage_failure_and_missing_current_log(self):
        for response in [b"ESP32 log storage is not mounted.\n", download.END + b"\n"]:
            with self.subTest(response=response), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(RuntimeError):
                    download.receive_logs(Port([response]), Path(tmp))
                self.assertFalse((Path(tmp) / "events_log.txt").exists())

    def test_read_failure_is_not_saved_as_a_measurement(self):
        port = Port([b"--- /spiffs/events_log.txt ---\nuptime=2s: value\n"
                     b"E (30) plant_log: Log read failed\n" + download.END + b"\n"])
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(RuntimeError):
                download.receive_logs(port, Path(tmp))
            self.assertFalse((Path(tmp) / "events_log.txt").exists())


if __name__ == "__main__":
    unittest.main()
