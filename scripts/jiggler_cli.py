import argparse
import sys
import time

import serial
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A
CDC_PID = 0x80C2


def detect_port():
    matches = [p for p in list_ports.comports() if p.vid == ESPRESSIF_VID and p.pid == CDC_PID]
    if len(matches) != 1:
        raise RuntimeError("Se esperaba un CDC ESP32-S2 (VID/PID 303A:80C2); encontrados: " + ", ".join(p.device for p in matches))
    return matches[0].device


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--command", required=True)
    parser.add_argument("--port")
    args = parser.parse_args()
    port_name = args.port or detect_port()
    with serial.Serial(port_name, 115200, timeout=0.2, write_timeout=2) as port:
        port.write(args.command.encode("ascii") + b"\n")
        deadline = time.monotonic() + 5
        output = []
        while time.monotonic() < deadline:
            line = port.readline().decode("ascii", errors="replace").strip()
            if not line or line.startswith("HEARTBEAT"):
                continue
            output.append(line)
            if line.startswith(("OK", "JIGGLE STATUS", "ERROR")):
                break
        if not output:
            raise TimeoutError(f"El ESP32 no respondió al comando {args.command!r} en {port_name}")
        print("\n".join(output))
        if output[-1].startswith("ERROR"):
            return 1
        return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Error Jiggler: {exc}", file=sys.stderr)
        sys.exit(1)
