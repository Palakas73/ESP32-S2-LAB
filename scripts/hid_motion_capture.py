"""Capture the ESP32's actual mouse deltas through Windows Raw Input.

Uses a message-only window and RIDEV_INPUTSINK; legacy mouse input remains
enabled. The normal run observes more than two continuous FAST orbits, then leaves OFF in
MEDIUM. --capture-only observes without starting movement or changing modes.

API references:
https://learn.microsoft.com/windows/win32/inputdev/using-raw-input
https://learn.microsoft.com/windows/win32/api/winuser/ns-winuser-rawmouse
https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getrawinputdeviceinfow
"""

import argparse
import ctypes
from ctypes import wintypes
import re
import sys
import time

import serial
from serial.tools import list_ports


VID = 0x303A
PID = 0x80C2
MAX_STEP = 2
MAX_COALESCED_STEP = 16
MAX_PACKETS = 1100
CAPTURE_SECONDS = 8.3
UINT_ERROR = 0xFFFFFFFF
WM_INPUT = 0x00FF
RID_INPUT = 0x10000003
RIDI_DEVICENAME = 0x20000007
RIDEV_INPUTSINK = 0x00000100
RIDEV_REMOVE = 0x00000001
PM_REMOVE = 0x0001
RIM_TYPEMOUSE = 0
MOUSE_MOVE_ABSOLUTE = 0x0001
MOUSE_MOVE_NOCOALESCE = 0x0008


class RAWINPUTHEADER(ctypes.Structure):
    _fields_ = [
        ("dwType", wintypes.DWORD),
        ("dwSize", wintypes.DWORD),
        ("hDevice", wintypes.HANDLE),
        ("wParam", wintypes.WPARAM),
    ]


class MOUSEBUTTONS(ctypes.Structure):
    _fields_ = [("usButtonFlags", wintypes.USHORT), ("usButtonData", wintypes.USHORT)]


class BUTTONUNION(ctypes.Union):
    _fields_ = [("ulButtons", wintypes.ULONG), ("buttons", MOUSEBUTTONS)]


class RAWMOUSE(ctypes.Structure):
    _fields_ = [
        ("usFlags", wintypes.USHORT),
        ("buttonUnion", BUTTONUNION),
        ("ulRawButtons", wintypes.ULONG),
        ("lLastX", wintypes.LONG),
        ("lLastY", wintypes.LONG),
        ("ulExtraInformation", wintypes.ULONG),
    ]


class RAWINPUTDEVICE(ctypes.Structure):
    _fields_ = [
        ("usUsagePage", wintypes.USHORT),
        ("usUsage", wintypes.USHORT),
        ("dwFlags", wintypes.DWORD),
        ("hwndTarget", wintypes.HWND),
    ]


class RAWINPUTDEVICELIST(ctypes.Structure):
    _fields_ = [("hDevice", wintypes.HANDLE), ("dwType", wintypes.DWORD)]


class MotionViolation(RuntimeError):
    pass


class RawMouseCapture:
    def __init__(self):
        if sys.platform != "win32":
            raise RuntimeError("La captura requiere una sesión de escritorio de Windows y Raw Input.")
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.packets = []
        self.names = {}
        self.error = None
        self.started = time.monotonic()
        self.window = None
        self.registered = False
        self.class_atom = 0
        self.class_name = f"ESP32RawMouseCapture_{id(self)}"

        self.wndproc_type = ctypes.WINFUNCTYPE(
            ctypes.c_ssize_t, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM
        )

        class WNDCLASSW(ctypes.Structure):
            _fields_ = [
                ("style", wintypes.UINT),
                ("lpfnWndProc", self.wndproc_type),
                ("cbClsExtra", ctypes.c_int),
                ("cbWndExtra", ctypes.c_int),
                ("hInstance", wintypes.HINSTANCE),
                ("hIcon", wintypes.HANDLE),
                ("hCursor", wintypes.HANDLE),
                ("hbrBackground", wintypes.HBRUSH),
                ("lpszMenuName", wintypes.LPCWSTR),
                ("lpszClassName", wintypes.LPCWSTR),
            ]

        self.WNDCLASSW = WNDCLASSW
        self.kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
        self.kernel32.GetModuleHandleW.restype = wintypes.HMODULE
        self.instance = self.kernel32.GetModuleHandleW(None)
        self.user32.RegisterClassW.argtypes = [ctypes.POINTER(WNDCLASSW)]
        self.user32.RegisterClassW.restype = wintypes.ATOM
        self.user32.UnregisterClassW.argtypes = [wintypes.LPCWSTR, wintypes.HINSTANCE]
        self.user32.UnregisterClassW.restype = wintypes.BOOL
        self.user32.CreateWindowExW.argtypes = [
            wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD,
            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
            wintypes.HWND, wintypes.HMENU, wintypes.HINSTANCE, wintypes.LPVOID,
        ]
        self.user32.CreateWindowExW.restype = wintypes.HWND
        self.user32.DestroyWindow.argtypes = [wintypes.HWND]
        self.user32.DestroyWindow.restype = wintypes.BOOL
        self.user32.DefWindowProcW.argtypes = [
            wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM,
        ]
        self.user32.DefWindowProcW.restype = ctypes.c_ssize_t
        self.user32.RegisterRawInputDevices.argtypes = [
            ctypes.POINTER(RAWINPUTDEVICE), wintypes.UINT, wintypes.UINT,
        ]
        self.user32.RegisterRawInputDevices.restype = wintypes.BOOL
        self.user32.GetRawInputData.argtypes = [
            wintypes.HANDLE, wintypes.UINT, wintypes.LPVOID,
            ctypes.POINTER(wintypes.UINT), wintypes.UINT,
        ]
        self.user32.GetRawInputData.restype = wintypes.UINT
        self.user32.GetRawInputDeviceInfoW.argtypes = [
            wintypes.HANDLE, wintypes.UINT, wintypes.LPVOID, ctypes.POINTER(wintypes.UINT),
        ]
        self.user32.GetRawInputDeviceInfoW.restype = wintypes.UINT
        self.user32.GetRawInputDeviceList.argtypes = [
            ctypes.POINTER(RAWINPUTDEVICELIST), ctypes.POINTER(wintypes.UINT), wintypes.UINT,
        ]
        self.user32.GetRawInputDeviceList.restype = wintypes.UINT
        self.user32.PeekMessageW.argtypes = [
            ctypes.POINTER(wintypes.MSG), wintypes.HWND, wintypes.UINT, wintypes.UINT, wintypes.UINT,
        ]
        self.user32.PeekMessageW.restype = wintypes.BOOL
        self.user32.TranslateMessage.argtypes = [ctypes.POINTER(wintypes.MSG)]
        self.user32.TranslateMessage.restype = wintypes.BOOL
        self.user32.DispatchMessageW.argtypes = [ctypes.POINTER(wintypes.MSG)]
        self.user32.DispatchMessageW.restype = ctypes.c_ssize_t

        # Keep callback and WNDCLASS objects alive until the window is destroyed.
        self.callback = self.wndproc_type(self._window_proc)
        self.window_class = WNDCLASSW()
        self.window_class.lpfnWndProc = self.callback
        self.window_class.hInstance = self.instance
        self.window_class.lpszClassName = self.class_name
        try:
            self.class_atom = self.user32.RegisterClassW(ctypes.byref(self.window_class))
            if not self.class_atom:
                self._win_error("RegisterClassW")
            # HWND_MESSAGE creates an invisible message-only window.
            self.window = self.user32.CreateWindowExW(
                0, self.class_name, "ESP32 Raw Input", 0, 0, 0, 0, 0,
                wintypes.HWND(-3), None, self.instance, None,
            )
            if not self.window:
                self._win_error("CreateWindowExW")
            registration = RAWINPUTDEVICE(1, 2, RIDEV_INPUTSINK, self.window)
            if not self.user32.RegisterRawInputDevices(
                ctypes.byref(registration), 1, ctypes.sizeof(RAWINPUTDEVICE)
            ):
                self._win_error("RegisterRawInputDevices")
            self.registered = True
            matches = self._target_devices()
            if not matches:
                raise RuntimeError(
                    "Raw Input no expone un ratón HID VID_303A&PID_80C2 en esta sesión. "
                    "No se ha validado el movimiento ni activado la placa."
                )
            for name in matches:
                print(f"Ratón Raw Input: {name}")
        except Exception:
            self.close()
            raise

    @staticmethod
    def _win_error(operation):
        error = ctypes.get_last_error()
        raise RuntimeError(
            f"{operation} falló: {ctypes.WinError(error)}. "
            "La sesión puede bloquear ventanas o Raw Input; no se ha validado el movimiento."
        )

    def _device_name(self, handle):
        key = int(handle or 0)
        if key not in self.names:
            count = wintypes.UINT(0)
            if self.user32.GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, None, ctypes.byref(count)) == UINT_ERROR:
                self._win_error("GetRawInputDeviceInfoW(size)")
            name = ctypes.create_unicode_buffer(count.value + 1)
            capacity = wintypes.UINT(len(name))
            if self.user32.GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, name, ctypes.byref(capacity)) == UINT_ERROR:
                self._win_error("GetRawInputDeviceInfoW(name)")
            self.names[key] = name.value
        return self.names[key]

    @staticmethod
    def _is_target(name):
        return "VID_303A&PID_80C2" in name.upper()

    def _target_devices(self):
        count = wintypes.UINT(0)
        if self.user32.GetRawInputDeviceList(None, ctypes.byref(count), ctypes.sizeof(RAWINPUTDEVICELIST)) == UINT_ERROR:
            self._win_error("GetRawInputDeviceList(size)")
        devices = (RAWINPUTDEVICELIST * count.value)()
        result = self.user32.GetRawInputDeviceList(devices, ctypes.byref(count), ctypes.sizeof(RAWINPUTDEVICELIST))
        if result == UINT_ERROR:
            self._win_error("GetRawInputDeviceList")
        return [
            name for device in devices[:result] if device.dwType == RIM_TYPEMOUSE
            for name in [self._device_name(device.hDevice)] if self._is_target(name)
        ]

    def _read_packet(self, handle):
        count = wintypes.UINT(0)
        header_size = ctypes.sizeof(RAWINPUTHEADER)
        if self.user32.GetRawInputData(handle, RID_INPUT, None, ctypes.byref(count), header_size) == UINT_ERROR:
            self._win_error("GetRawInputData(size)")
        buffer = ctypes.create_string_buffer(count.value)
        result = self.user32.GetRawInputData(handle, RID_INPUT, buffer, ctypes.byref(count), header_size)
        if result == UINT_ERROR:
            self._win_error("GetRawInputData")
        if result < header_size:
            raise RuntimeError("Raw Input devolvió una cabecera incompleta; captura no validada.")
        header = RAWINPUTHEADER.from_buffer_copy(buffer)
        # Touchpad/synthetic mouse messages may have a null device handle.
        if header.dwType != RIM_TYPEMOUSE or not header.hDevice or not self._is_target(self._device_name(header.hDevice)):
            return
        if result < header_size + ctypes.sizeof(RAWMOUSE):
            raise RuntimeError("Raw Input devolvió RAWMOUSE incompleto; captura no validada.")
        mouse = RAWMOUSE.from_buffer_copy(buffer, header_size)
        dx, dy = int(mouse.lLastX), int(mouse.lLastY)
        buttons = int(mouse.buttonUnion.buttons.usButtonFlags)
        button_data = int(mouse.buttonUnion.buttons.usButtonData)
        self.packets.append((time.monotonic() - self.started, dx, dy, int(mouse.usFlags), buttons, button_data))
        # Windows may combine several USB reports into one relative event.
        # Enforce the per-report cap on uncoalesced events; verify the actual
        # USB emitter's maxima through CDC below and always bound host jumps.
        limit = MAX_STEP if mouse.usFlags & MOUSE_MOVE_NOCOALESCE else MAX_COALESCED_STEP
        if abs(dx) > limit or abs(dy) > limit:
            raise MotionViolation(f"Evento Raw Input fuera del límite de {limit}: dx={dx}, dy={dy}, flags={mouse.usFlags}; se detiene la placa.")
        if len(self.packets) > MAX_PACKETS:
            raise MotionViolation(f"Más de {MAX_PACKETS} informes en la captura; se detiene la placa.")
        if mouse.usFlags & MOUSE_MOVE_ABSOLUTE:
            raise MotionViolation("Se recibió movimiento absoluto inesperado; se detiene la placa.")
        if buttons or button_data or mouse.ulRawButtons:
            raise MotionViolation("Se recibieron botones o rueda inesperados; se detiene la placa.")

    def _window_proc(self, window, message, wparam, lparam):
        if message == WM_INPUT:
            try:
                self._read_packet(wintypes.HANDLE(lparam))
            except Exception as exc:
                if self.error is None:
                    self.error = exc
        # DefWindowProc releases the foreground WM_INPUT resources as documented.
        return self.user32.DefWindowProcW(window, message, wparam, lparam)

    def pump(self, check_error=True):
        message = wintypes.MSG()
        while self.user32.PeekMessageW(ctypes.byref(message), None, 0, 0, PM_REMOVE):
            self.user32.TranslateMessage(ctypes.byref(message))
            self.user32.DispatchMessageW(ctypes.byref(message))
            if self.error and check_error:
                raise self.error
        if self.error and check_error:
            raise self.error

    def clear(self):
        self.pump()
        self.packets.clear()
        self.started = time.monotonic()

    def close(self):
        if self.registered:
            registration = RAWINPUTDEVICE(1, 2, RIDEV_REMOVE, None)
            self.user32.RegisterRawInputDevices(ctypes.byref(registration), 1, ctypes.sizeof(RAWINPUTDEVICE))
            self.registered = False
        if self.window:
            self.user32.DestroyWindow(self.window)
            self.window = None
        if self.class_atom:
            self.user32.UnregisterClassW(self.class_name, self.instance)
            self.class_atom = 0


def find_cdc():
    ports = [port.device for port in list_ports.comports() if port.vid == VID and port.pid == PID]
    if len(ports) != 1:
        raise RuntimeError(f"Se esperaba un CDC VID/PID 303A:80C2; encontrados: {ports}")
    return ports[0]


def command(port, capture, text, expected, check_error=True):
    port.write(text.encode("ascii") + b"\n")
    pending = bytearray()
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        capture.pump(check_error=check_error)
        pending.extend(port.read(port.in_waiting or 1))
        while b"\n" in pending:
            line, _, remainder = pending.partition(b"\n")
            pending = bytearray(remainder)
            decoded = line.decode("ascii", errors="replace").strip()
            if decoded.startswith("ERROR"):
                raise RuntimeError(f"{text}: {decoded}")
            if expected in decoded:
                return decoded
    raise RuntimeError(f"CDC no respondió a {text}; no se ha completado la validación.")


def status_fields(line):
    return dict(re.findall(r"([a-z_]+)=([^ ]+)", line))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-only", action="store_true", help="Observar sin activar ni cambiar modo; ante un fallo se envía OFF.")
    args = parser.parse_args()
    capture = RawMouseCapture()
    port = None
    finished_status = None
    setup_started = False
    failure = None
    try:
        port_name = find_cdc()
        port = serial.Serial(port_name, 115200, timeout=0.01, write_timeout=1)
        print(f"CDC: {port_name}; ventana oculta registrada para Raw Input.")
        if not args.capture_only:
            setup_started = True
            command(port, capture, "JIGGLE OFF", "OK active=0")
            command(port, capture, "JIGGLE FAST", "OK mode=FAST")
            command(port, capture, "JIGGLE SCREEN 1920 1080", "OK screen_width=1920")
        capture.clear()
        if not args.capture_only:
            command(port, capture, "JIGGLE ON", "OK active=1")
        deadline = capture.started + CAPTURE_SECONDS
        while time.monotonic() < deadline:
            capture.pump()
            time.sleep(0.001)
    except BaseException as exc:
        failure = exc
    finally:
        if port:
            try:
                if setup_started or failure:
                    command(port, capture, "JIGGLE OFF", "OK active=0", check_error=False)
                if setup_started:
                    command(port, capture, "JIGGLE MEDIUM", "OK mode=MEDIUM", check_error=False)
                finished_status = command(port, capture, "JIGGLE STATUS", "JIGGLE STATUS", check_error=False)
                # Include the one report that could already be in flight at OFF.
                drain_until = time.monotonic() + 0.1
                while time.monotonic() < drain_until:
                    capture.pump(check_error=False)
                    time.sleep(0.001)
            except Exception as exc:
                if failure is None:
                    failure = exc
                else:
                    print(f"Además falló la parada/consulta CDC: {exc}", file=sys.stderr)
            port.close()
        capture.close()
    if failure:
        raise failure
    if capture.error:
        raise capture.error
    packets = capture.packets
    sum_x = sum(packet[1] for packet in packets)
    sum_y = sum(packet[2] for packet in packets)
    max_x = max((abs(packet[1]) for packet in packets), default=0)
    max_y = max((abs(packet[2]) for packet in packets), default=0)
    merged = sum(1 for packet in packets if abs(packet[1]) > MAX_STEP or abs(packet[2]) > MAX_STEP)
    print(f"Raw Input observado: eventos={len(packets)} suma=({sum_x}, {sum_y}) máximo=({max_x}, {max_y}) agrupados_mayores_de_2={merged}")
    if finished_status:
        print(f"Firmware: {finished_status}")
    if args.capture_only:
        if not packets:
            raise AssertionError("No se observaron informes del HID objetivo. La captura pasiva no valida movimiento.")
        print("Captura pasiva completada; no se solicitó una órbita y no se afirma cierre de trayectoria.")
        return
    if len(packets) < 750:
        raise AssertionError(f"Solo se recibieron {len(packets)} informes del HID objetivo; no cubren dos vueltas continuas.")
    # Integrate actual USB deltas. A partial final turn need not sum to zero;
    # every sample must stay on the same ring, without cumulative drift.
    position_x = position_y = 0
    for _, dx, dy, *_ in packets:
        position_x += dx; position_y += dy
        radius = ((position_x + 100) ** 2 + position_y ** 2) ** .5
        if not 97 <= radius <= 103:
            raise AssertionError(f"La órbita HID acumuló deriva: ({position_x}, {position_y}), radio={radius:.2f}.")
    max_gap = max(b[0] - a[0] for a, b in zip(packets, packets[1:]))
    if max_gap > .4:
        raise AssertionError(f"Pausa entre informes de {max_gap:.3f}s; las vueltas deben continuar sin espera.")
    values = status_fields(finished_status)
    for key in ("tx_reports", "tx_sum_x", "tx_sum_y", "tx_max_abs_x", "tx_max_abs_y", "pending_x", "pending_y"):
        if key not in values:
            raise AssertionError(f"Falta {key} en STATUS; no puede compararse la captura con el transporte del firmware.")
    firmware_reports = int(values["tx_reports"])
    if int(values["tx_max_abs_x"]) > MAX_STEP or int(values["tx_max_abs_y"]) > MAX_STEP:
        raise AssertionError("El emisor USB aceptó un incremento mayor de 2 por eje.")
    firmware_x, firmware_y = int(values["tx_sum_x"]), int(values["tx_sum_y"])
    if firmware_reports < len(packets) or firmware_reports > MAX_PACKETS:
        raise AssertionError(f"Contabilidad de informes incoherente: firmware={firmware_reports}, Raw Input={len(packets)}.")
    if (firmware_x, firmware_y) != (sum_x, sum_y):
        raise AssertionError(f"CDC y Raw Input difieren: firmware=({firmware_x},{firmware_y}), host=({sum_x},{sum_y}).")
    if int(values["pending_x"]) or int(values["pending_y"]):
        raise AssertionError("El firmware dejó movimiento pendiente tras detenerse.")
    if values["active"] != "0" or values["mode"] != "MEDIUM":
        raise AssertionError("El dispositivo no quedó apagado en MEDIUM.")
    print(f"PASS: más de dos vueltas HID continuas, radio estable, intervalo máximo {max_gap:.3f}s, sin botones/rueda; apagado en MEDIUM.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FALLO DE CAPTURA HID: {exc}", file=sys.stderr)
        sys.exit(1)
