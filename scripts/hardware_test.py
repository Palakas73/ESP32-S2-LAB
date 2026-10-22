import sys, time
import serial
from serial.tools import list_ports

VID_ALLOW = {0x303A, 0x10C4, 0x1A86, 0x0403}

def read_until(port, token, timeout=8):
    data = bytearray()
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        data.extend(port.read(port.in_waiting or 1))
        if token in data:
            return bytes(data)
    raise AssertionError(f"No se recibió {token!r}; recibido: {data.decode(errors='replace')}")

def find_ports():
    return [p for p in list_ports.comports() if p.vid in VID_ALLOW]

def wait_for_port(timeout=12):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        found = find_ports()
        if len(found) == 1:
            return found[0].device
        time.sleep(0.2)
    raise TimeoutError("El USB serie no reapareció después del reinicio")

def main():
    found = find_ports()
    if len(found) != 1:
        raise RuntimeError("Se esperaba un puerto serie compatible; encontrados: " + ", ".join(p.device for p in found))
    print(f"Puerto físico detectado: {found[0].device}")
    ser = serial.Serial(found[0].device, 115200, timeout=0.25, write_timeout=2)
    reboot_requested = False
    try:
        time.sleep(2)
        boot = ser.read(ser.in_waiting or 1)
        print("Enumeración serie: OK")
        if b"ESP32-S2 Mini diagnostic firmware" not in boot:
            print("Esperando heartbeat; el mensaje de arranque ya salió del búfer.")
        read_until(ser, b"HEARTBEAT")
        print("Recepción y heartbeat: OK")
        ser.write(b"PING\n")
        assert b"PONG" in read_until(ser, b"PONG")
        print("PING -> PONG: OK")
        ser.write(b"INFO\n")
        info = read_until(ser, b"END_INFO")
        for key in (b"model=", b"revision=", b"flash_bytes=", b"psram_bytes=", b"uptime_ms="):
            assert key in info, f"Falta {key!r}: {info!r}"
        print("INFO: OK")
        decoded = info.decode(errors="replace")
        print(decoded.strip())
        before = int(next(line.split("=", 1)[1] for line in decoded.splitlines() if line.startswith("uptime_ms=")))
        ser.write(b"REBOOT\n")
        read_until(ser, b"REBOOTING")
        reboot_requested = True
    finally:
        try:
            ser.close()
        except (serial.SerialException, OSError):
            if not reboot_requested:
                raise
    time.sleep(1.2)
    new_port = wait_for_port()
    with serial.Serial(new_port, 115200, timeout=0.25, write_timeout=2) as ser:
        read_until(ser, b"HEARTBEAT", timeout=12)
        ser.write(b"INFO\n")
        reboot_info = read_until(ser, b"END_INFO")
        after = int(next(line.split("=", 1)[1] for line in reboot_info.decode(errors="replace").splitlines() if line.startswith("uptime_ms=")))
        assert after < before, f"El uptime no se reinició: antes={before}, después={after}"
        print(f"Reconexión tras reinicio: OK en {new_port}; uptime {before} -> {after} ms")

if __name__ == '__main__':
    try: main()
    except Exception as e:
        print(f"FALLO DE PRUEBA FÍSICA: {e}", file=sys.stderr)
        sys.exit(1)
