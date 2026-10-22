import re
import subprocess
import sys
import time

import serial
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A
CDC_PID = 0x80C2
VID_ALLOW = {ESPRESSIF_VID, 0x10C4, 0x1A86, 0x0403}
PROFILES = {
    "SLOW": (3500, 8000, 15000),
    "MEDIUM": (1800, 3000, 7000),
    "FAST": (700, 500, 2000),
}


def find_ports():
    return list(list_ports.comports())


def find_esp_port():
    found = [p for p in find_ports() if p.vid == ESPRESSIF_VID and p.pid in (CDC_PID, 0x0002)]
    if len(found) != 1:
        raise RuntimeError("Se esperaba un puerto CDC Espressif; encontrados: " + ", ".join(p.device for p in found))
    return found[0]


def read_until(port, token, timeout=5):
    data = bytearray()
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        data.extend(port.read(port.in_waiting or 1))
        if token in data:
            return bytes(data)
    raise AssertionError(f"No se recibió {token!r}; recibido: {data.decode(errors='replace')}")


def command(port, text, expected, timeout=4):
    port.write(text.encode("ascii") + b"\n")
    response = read_until(port, expected.encode("ascii"), timeout)
    decoded = response.decode(errors="replace")
    lines = [line.strip() for line in decoded.splitlines() if line.strip()]
    if expected == "END_INFO":
        return decoded
    result = next((line for line in lines if expected in line), "")
    if not result:
        raise AssertionError(f"Respuesta incompleta para {text}: {decoded}")
    return result


def status(port):
    return command(port, "JIGGLE STATUS", "JIGGLE STATUS")


def fields(line):
    return dict(re.findall(r"([a-z_]+)=([^ ]+)", line))


def verify_windows_mouse_interface():
    result = subprocess.run(
        ["pnputil", "/enum-devices", "/connected", "/class", "Mouse"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=15,
    )
    if result.returncode != 0:
        raise RuntimeError("pnputil no pudo enumerar dispositivos de clase Mouse: " + result.stderr.strip())
    if not re.search(r"VID_303A&PID_80C2", result.stdout, re.IGNORECASE):
        raise AssertionError("Windows no muestra un dispositivo de clase Mouse con VID/PID 303A:80C2")
    print("Enumeración Windows HID Mouse (VID/PID 303A:80C2): OK")


def check_virtual_bounds(line, width, height):
    values = fields(line)
    x, y = int(values["virtual_x"]), int(values["virtual_y"])
    assert 0 <= x < width and 0 <= y < height, f"Posición virtual fuera de límites: ({x}, {y}) / {width}x{height}"
    return x, y, values


def wait_for_port(timeout=12):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            return find_esp_port().device
        except RuntimeError:
            time.sleep(0.2)
    raise TimeoutError("El CDC Espressif no reapareció después del reinicio")


def main():
    port_info = find_esp_port()
    print(f"CDC físico: {port_info.device}, VID/PID={port_info.vid:04X}:{port_info.pid:04X}")
    ser = serial.Serial(port_info.device, 115200, timeout=0.2, write_timeout=2)
    reboot_requested = False
    try:
        time.sleep(1.2)
        read_until(ser, b"HEARTBEAT", timeout=5)
        print("Arranque y heartbeat: OK")

        assert "PONG" in command(ser, "PING", "PONG")
        print("PING -> PONG: OK")
        info = command(ser, "INFO", "END_INFO")
        for key in ("model=ESP32-S2", "flash_bytes=4194304", "psram_bytes=2097152", "usb=CDC+HID-Mouse"):
            assert key in info, f"Falta {key} en INFO: {info}"
        print("INFO de hardware: OK")

        initial = fields(status(ser))
        assert initial["active"] == "0", "El Jiggler no arrancó desactivado"
        assert initial["mode"] == "SLOW"
        print("Estado inicial desactivado: OK")

        for mode, profile in PROFILES.items():
            reply = command(ser, f"JIGGLE {mode}", f"OK mode={mode}")
            values = fields(reply)
            assert values["active"] == "0", f"Seleccionar {mode} activó el Jiggler"
            actual = tuple(int(values[k]) for k in ("trajectory_ms", "pause_min_ms", "pause_max_ms"))
            assert actual == profile, f"Perfil {mode} incorrecto: {actual}"
        print("Selección y parámetros de los tres modos sin autoactivación: OK")

        reply = command(ser, "JIGGLE SCREEN 640 480", "OK screen_width=640")
        assert "screen_height=480" in reply
        assert "ERROR" in command(ser, "JIGGLE SCREEN 100 100", "ERROR")
        values = fields(status(ser))
        assert values["screen_width"] == "640" and values["screen_height"] == "480"
        check_virtual_bounds(status(ser), 640, 480)
        print("Configuración de pantalla y rechazo de límites inválidos: OK")

        command(ser, "JIGGLE FAST", "OK mode=FAST")
        command(ser, "JIGGLE ON", "OK active=1")
        moving_positions = []
        end = time.monotonic() + 0.95
        while time.monotonic() < end:
            x, y, values = check_virtual_bounds(status(ser), 640, 480)
            moving_positions.append((x, y))
            assert values["active"] == "1"
            time.sleep(0.035)
        assert len(set(moving_positions)) > 1, "El movimiento virtual no avanzó durante FAST"
        command(ser, "JIGGLE MEDIUM", "OK mode=MEDIUM")
        assert fields(status(ser))["active"] == "1", "El cambio de modo detuvo el Jiggler activo"
        command(ser, "JIGGLE SLOW", "OK mode=SLOW")
        assert fields(status(ser))["active"] == "1"
        print("Activación, movimiento acotado y cambio de velocidad en marcha: OK")

        command(ser, "JIGGLE OFF", "OK active=0")
        stopped = status(ser)
        assert fields(stopped)["active"] == "0"
        time.sleep(0.15)
        assert status(ser) == stopped, "La posición virtual cambió después de desactivar"
        print("Desactivación inmediata y posición detenida: OK")

        command(ser, "JIGGLE SCREEN 1920 1080", "OK screen_width=1920")
        command(ser, "JIGGLE ON", "OK active=1")
        command(ser, "JIGGLE OFF", "OK active=0")
        verify_windows_mouse_interface()

        ser.write(b"REBOOT\n")
        reboot_requested = True
        try:
            read_until(ser, b"REBOOTING")
        except (serial.SerialException, OSError):
            print("El USB se desconectó al reiniciar antes de entregar el acuse CDC.")
    finally:
        try:
            ser.close()
        except (serial.SerialException, OSError):
            if not reboot_requested:
                raise

    time.sleep(1.2)
    new_port = wait_for_port()
    with serial.Serial(new_port, 115200, timeout=0.2, write_timeout=2) as ser:
        read_until(ser, b"HEARTBEAT", timeout=12)
        reboot_status = fields(status(ser))
        assert reboot_status["active"] == "0", "El Jiggler se activó después de reiniciar"
        assert reboot_status["mode"] == "SLOW", "El modo inicial tras reinicio no es SLOW"
    print(f"Reconexión tras reinicio y estado seguro: OK en {new_port}")
    print("Pruebas físicas CDC + HID completadas; no se enviaron clics ni teclas.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FALLO DE PRUEBA FÍSICA: {exc}", file=sys.stderr)
        sys.exit(1)
