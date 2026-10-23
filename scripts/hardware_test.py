import re
import subprocess
import sys
import time

import serial
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A
CDC_PID = 0x80C2
PROFILES = {
    "SLOW": (8000, 0, 0),
    "MEDIUM": (6000, 0, 0),
    "FAST": (4000, 0, 0),
}


def find_ports():
    return list(list_ports.comports())


def find_esp_port():
    found = [p for p in find_ports() if p.vid == ESPRESSIF_VID and p.pid == CDC_PID]
    if len(found) != 1:
        raise RuntimeError("Se esperaba un puerto CDC Espressif; encontrados: " + ", ".join(p.device for p in found))
    return found[0]


def read_until(port, token, timeout=5):
    data = bytearray()
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        data.extend(port.read(port.in_waiting or 1))
        token_at = data.find(token)
        if token_at >= 0 and b'\n' in data[token_at:]:
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


def check_circle(line, width, height, require_orbit=False):
    x, y, values = check_virtual_bounds(line, width, height)
    center_x, center_y = int(values["circle_center_x"]), int(values["circle_center_y"])
    radius = ((x - center_x) ** 2 + (y - center_y) ** 2) ** 0.5
    assert values["motion"] == "circle"
    assert int(values["circle_radius_px"]) == 100
    assert radius <= 101.0, f"La posición virtual excedió el radio del círculo: {radius:.2f}px"
    if require_orbit:
        assert radius >= 99.0, f"La trayectoria no alcanzó el radio de 100px: {radius:.2f}px"
    return x, y, values


def wait_for_port(timeout=12):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            port_name = find_esp_port().device
            return serial.Serial(port_name, 115200, timeout=0.2, write_timeout=2), port_name
        except (RuntimeError, serial.SerialException, OSError):
            time.sleep(0.2)
    raise TimeoutError("El CDC Espressif no reapareció después del reinicio")


def reboot_and_check(ser, expected_mode):
    try:
        ser.write(b"REBOOT\n")
        try:
            read_until(ser, b"REBOOTING")
        except (serial.SerialException, OSError):
            pass  # Reset may remove USB before the CDC acknowledgement arrives.
    except (serial.SerialException, OSError):
        pass
    try:
        ser.close()
    except (serial.SerialException, OSError):
        pass
    time.sleep(1.2)
    fresh, port_name = wait_for_port()
    read_until(fresh, b"HEARTBEAT", timeout=12)
    values = fields(status(fresh))
    assert values["active"] == "0", "El Jiggler se activó después de reiniciar"
    assert values["mode"] == expected_mode, f"Modo no persistió: {values['mode']} != {expected_mode}"
    assert "PONG" in command(fresh, "PING", "PONG")
    return fresh, port_name


def main():
    port_info = find_esp_port()
    print(f"CDC físico: {port_info.device}, VID/PID={port_info.vid:04X}:{port_info.pid:04X}")
    ser = serial.Serial(port_info.device, 115200, timeout=0.2, write_timeout=2)
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
        assert initial["mode"] in ("SLOW", "MEDIUM", "FAST")
        assert initial["button_gpio"] == "0" and initial["led_gpio"] == "15"
        assert initial["hid_step_limit"] == "2" and initial["hid_interval_ms"] == "8"
        assert initial["gesture_test"] == "PASS", "Falló el autodiagnóstico BOOT/estado del motor"
        assert initial["led_test"] == "PASS", "Falló la autoprueba de reactivación y temporización del LED"
        assert initial["led_active_low"] == "0", "La polaridad del LED GPIO15 está invertida"
        assert initial["led_on"] == "0" and initial["led_pin_level"] == "0"
        assert initial["led_window_remaining_ms"] == "0"
        print("LED: parpadeo, ventanas repetidas de 4 s, clic/doble clic y rollover: autoprueba OK; GPIO15 apagado en LOW")
        print(f"Estado inicial desactivado y controles físicos GPIO0/GPIO15: OK (modo {initial['mode']})")
        print("Autopruebas BOOT/motor: antirrebote, pulsación corta/doble, círculo de 100px y apagado: OK")

        for mode, profile in PROFILES.items():
            reply = command(ser, f"JIGGLE {mode}", f"OK mode={mode}")
            values = fields(reply)
            assert values["active"] == "0", f"Seleccionar {mode} activó el Jiggler"
            actual = tuple(int(values[k]) for k in ("trajectory_ms", "pause_min_ms", "pause_max_ms"))
            assert actual == profile, f"Perfil {mode} incorrecto: {actual}"
            off_led = fields(status(ser))
            assert off_led['led_on'] == '0' and off_led['led_pin_level'] == '0' and off_led['led_window_remaining_ms'] == '0'
        print("Selección y parámetros de los tres modos sin autoactivación: OK")

        reply = command(ser, "JIGGLE SCREEN 640 480", "OK screen_width=640")
        assert "screen_height=480" in reply
        assert "ERROR" in command(ser, "JIGGLE SCREEN 100 100", "ERROR")
        values = fields(status(ser))
        assert values["screen_width"] == "640" and values["screen_height"] == "480"
        check_circle(status(ser), 640, 480)
        print("Configuración de pantalla y rechazo de límites inválidos: OK")

        command(ser, "JIGGLE FAST", "OK mode=FAST")
        start_x, start_y, _ = check_circle(status(ser), 640, 480, require_orbit=True)
        command(ser, "JIGGLE ON", "OK active=1")
        moving_positions = []
        end = time.monotonic() + 8.3
        while time.monotonic() < end:
            x, y, values = check_circle(status(ser), 640, 480, require_orbit=True)
            moving_positions.append((x, y))
            assert values["active"] == "1"
            assert values["path"] == "MOVING", "Apareció una pausa entre vueltas"
            time.sleep(0.035)
        assert len(set(moving_positions)) > 20, "El movimiento virtual no avanzó durante FAST"
        horizontal_span = max(x for x, _ in moving_positions) - min(x for x, _ in moving_positions)
        vertical_span = max(y for _, y in moving_positions) - min(y for _, y in moving_positions)
        assert 190 <= horizontal_span <= 201 and 190 <= vertical_span <= 201, (
            f"La vuelta no cubrió el diámetro previsto: {horizontal_span}x{vertical_span}px"
        )
        end_x, end_y, transmitted = check_circle(status(ser), 640, 480, require_orbit=True)
        assert transmitted['pending_x'] == '0' and transmitted['pending_y'] == '0', "Quedó movimiento sin transmitir"
        assert int(transmitted['submitted_x']) == end_x and int(transmitted['submitted_y']) == end_y
        assert 750 <= int(transmitted['tx_reports']) <= 1100, "Número de informes HID aceptados anómalo en dos vueltas"
        assert int(transmitted['tx_max_abs_x']) <= 2 and int(transmitted['tx_max_abs_y']) <= 2
        assert int(transmitted['tx_sum_x']) == end_x - start_x and int(transmitted['tx_sum_y']) == end_y - start_y
        assert transmitted['led_on'] == '0' and transmitted['led_pin_level'] == '0' and transmitted['led_window_remaining_ms'] == '0'
        command(ser, "JIGGLE MEDIUM", "OK mode=MEDIUM")
        assert fields(status(ser))["active"] == "1", "El cambio de modo detuvo el Jiggler activo"
        command(ser, "JIGGLE SLOW", "OK mode=SLOW")
        assert fields(status(ser))["active"] == "1"
        print("Órbita de 100px, más de dos vueltas continuas sin pausa, transmisión HID y cambio de velocidad: OK")

        command(ser, "JIGGLE OFF", "OK active=0")
        stopped = status(ser)
        stopped_fields = fields(stopped)
        assert stopped_fields["active"] == "0"
        assert stopped_fields['led_on'] == '0' and stopped_fields['led_pin_level'] == '0' and stopped_fields['led_window_remaining_ms'] == '0'
        time.sleep(0.15)
        assert status(ser) == stopped, "La posición virtual cambió después de desactivar"
        print("Desactivación inmediata y posición detenida: OK")

        command(ser, "JIGGLE SCREEN 1920 1080", "OK screen_width=1920")
        for _ in range(2):
            command(ser, "JIGGLE ON", "OK active=1")
            lit = fields(status(ser))
            assert lit['led_on'] == '1' and lit['led_pin_level'] == '1' and int(lit['led_window_remaining_ms']) > 3000
            command(ser, "JIGGLE OFF", "OK active=0")
            dark = fields(status(ser))
            assert dark['led_on'] == '0' and dark['led_pin_level'] == '0' and dark['led_window_remaining_ms'] == '0'
        print("LED: OFF inmediato cancela el temporizador; dos ciclos ON/OFF reales reencienden y apagan GPIO15: OK")
        verify_windows_mouse_interface()

        command(ser, "JIGGLE FAST", "OK mode=FAST")
        ser, new_port = reboot_and_check(ser, "FAST")
        print(f"Modo FAST persistido, arranque apagado y CDC reenumerado: OK en {new_port}")

        command(ser, "JIGGLE MEDIUM", "OK mode=MEDIUM")
        ser, new_port = reboot_and_check(ser, "MEDIUM")
        print(f"Modo MEDIUM persistido, arranque apagado y CDC reenumerado: OK en {new_port}")
    finally:
        try:
            if ser.is_open:
                command(ser, "JIGGLE OFF", "OK active=0", timeout=2)
        except (serial.SerialException, OSError, AssertionError):
            pass
        try:
            ser.close()
        except (serial.SerialException, OSError):
            pass
    print("Pruebas físicas CDC + HID completadas; no se enviaron clics ni teclas.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FALLO DE PRUEBA FÍSICA: {exc}", file=sys.stderr)
        sys.exit(1)
