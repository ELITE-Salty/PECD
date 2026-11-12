
import serial

# Raspberry Pi 4 example.
# On Pi 5, check the correct UART device first.
UART_PORT = "/dev/serial0"

with serial.Serial(
    UART_PORT,
    baudrate=115200,
    timeout=1
) as ser:

    print("UART receiver started", flush=True)

    while True:
        line = ser.readline().decode(
            "utf-8", errors="replace"
        ).strip()

        if not line:
            continue

        if line == "PING":
            ser.write(b"READY\n")
            print("ESP32 connected", flush=True)

        elif line == "Hello from ESP32":
            print(f"Received: {line}", flush=True)
            ser.write(b"ACK\n")
