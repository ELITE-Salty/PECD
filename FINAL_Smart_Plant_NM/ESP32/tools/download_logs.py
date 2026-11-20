"""Copy the unoptimised firmware's saved logs over USB without resetting it.

Run in an ESP-IDF terminal: python tools/download_logs.py COM5
Requires pyserial, which is included in the ESP-IDF Python environment.
"""

import argparse
from datetime import datetime
from pathlib import Path
import time


HEADERS = {
    b"--- /spiffs/events_prev.txt ---": "events_prev.txt",
    b"--- /spiffs/events_log.txt ---": "events_log.txt",
}
END = b"--- End of saved logs ---"
DEFAULT_OUTPUT = Path(__file__).resolve().parents[1] / "downloaded_logs"


def receive_logs(port, output, timeout=180):
    """Keep incomplete transfers as .partial files, never as finished logs."""
    pending = bytearray()
    received = []
    stream = None
    deadline = time.monotonic() + timeout
    port.write(b"logs\n")
    try:
        while time.monotonic() < deadline:
            pending.extend(port.read(4096))
            while b"\n" in pending:
                raw, _, remainder = pending.partition(b"\n")
                pending = bytearray(remainder)
                line = bytes(raw).rstrip(b"\r")
                if line in HEADERS:
                    name = HEADERS[line]
                    if name in received or (received and name == "events_prev.txt"):
                        raise RuntimeError("Unexpected log header; retry with the monitor closed.")
                    if stream:
                        stream.close()
                    received.append(name)
                    stream = (output / (name + ".partial")).open("wb")
                    print(f"Downloading {name}...", flush=True)
                elif line == END:
                    if "events_log.txt" not in received:
                        raise RuntimeError("The ESP32 has no current events_log.txt to download.")
                    if stream:
                        stream.close()
                        stream = None
                    for name in received:
                        (output / (name + ".partial")).replace(output / name)
                    return [output / name for name in received]
                elif b"not mounted" in line or b"plant_log:" in line:
                    raise RuntimeError(line.decode("utf-8", errors="replace"))
                elif stream:
                    # Stored records are uptime-prefixed; reject console errors/noise.
                    if not line.startswith(b"uptime="):
                        raise RuntimeError(f"Unexpected data during download: {line[:160]!r}; incomplete files kept as .partial.")
                    stream.write(line + b"\n")
            if len(pending) > 4096:
                raise RuntimeError("Unexpected oversized serial line.")
        raise TimeoutError("Download timed out; incomplete files kept as .partial. Check USB and close the monitor.")
    finally:
        if stream:
            stream.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="ESP32 USB serial port, e.g. COM5")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help="Parent directory for dated download folders")
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        parser.exit(1, "Use an ESP-IDF terminal, or install pyserial with: python -m pip install pyserial\n")

    try:
        # Avoid the automatic reset normally caused by opening the serial port.
        with serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=3) as port:
            port.dtr = False
            port.rts = False
            port.port = args.port
            port.open()
            port.reset_input_buffer()
            output = args.output.resolve() / datetime.now().strftime("%Y-%m-%d_%H-%M-%S-%f")
            output.mkdir(parents=True, exist_ok=False)
            print("Reading ESP32 storage; large logs can take about a minute.", flush=True)
            for path in receive_logs(port, output):
                print(f"Saved: {path}")
            print("Done. The original logs remain on the ESP32.")
    except (OSError, RuntimeError, TimeoutError, serial.SerialException) as exc:
        parser.exit(1, f"Download failed: {exc}\nClose idf.py monitor or any other app using the port, then retry.\n")
    except KeyboardInterrupt:
        parser.exit(1, "Download cancelled; unfinished files have a .partial extension.\n")


if __name__ == "__main__":
    main()
