import sys
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A
CDC_PID = 0x80C2
BOOTLOADER_PID = 0x0002

def ports():
    return list(list_ports.comports())

if __name__ == "__main__":
    found = ports()
    if len(sys.argv) > 1 and sys.argv[1] == "detect":
        candidates = [p for p in found if p.vid == ESPRESSIF_VID and p.pid == CDC_PID]
        if not candidates:
            candidates = [p for p in found if p.vid == ESPRESSIF_VID and p.pid == BOOTLOADER_PID]
        if len(candidates) == 1:
            print(candidates[0].device)
        else:
            print("Compatible ports: " + ", ".join(p.device for p in candidates), file=sys.stderr)
            sys.exit(2)
    else:
        for p in found:
            print(f"{p.device}\tVID={p.vid!s}\tPID={p.pid!s}\t{p.description}\t{p.hwid}")
