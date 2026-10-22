# ESP32-S2-LAB — USB HID Mouse Jiggler

Firmware para LOLIN S2 Mini (ESP32-S2FNR2, 4 MB flash y 2 MB PSRAM) que se presenta en Windows como dispositivo USB compuesto CDC + HID Mouse. El puerto CDC conserva los comandos de diagnóstico y controla el movimiento del ratón. El Jiggler arranca siempre desactivado.

## Comportamiento

- Sin clics ni informes de teclado; los informes de ratón mantienen `buttons=0`.
- Trayectorias amplias, aleatorias y suavizadas con curvas Bezier y aceleración/desaceleración smoothstep.
- Máquina de estados no bloqueante. CDC se atiende en cada iteración; los informes relativos se limitan a ±127 por informe y los incrementos pendientes se vacían sin bloquear.
- `JIGGLE OFF` vacía los incrementos pendientes e interrumpe la trayectoria inmediatamente.
- Coordenadas virtuales estimadas y acotadas por las dimensiones configuradas. Windows puede aplicar aceleración o clipping y el firmware no puede leer la posición absoluta: por eso la posición virtual no tiene por qué coincidir con el cursor real.

### Modos iniciales

| Modo | Trayectoria | Pausa aleatoria |
| --- | ---: | ---: |
| SLOW | 3500 ms | 8000–15000 ms |
| MEDIUM | 1800 ms | 3000–7000 ms |
| FAST | 700 ms | 500–2000 ms |

Los valores son parámetros iniciales definidos en `src/jiggler_engine.cpp`. Cambiar el modo mientras está apagado no lo activa; cambiarlo encendido conserva la activación.

## Comandos CDC

Se puede enviar una línea por USB CDC a 115200 baudios:

- `PING` → `PONG`
- `INFO` → datos de hardware y `END_INFO`
- `REBOOT` → reinicia el microcontrolador
- `JIGGLE ON` → activa con el último modo seleccionado
- `JIGGLE OFF` → desactiva y detiene movimiento pendiente
- `JIGGLE SLOW`, `JIGGLE MEDIUM`, `JIGGLE FAST` → selecciona modo sin activar si estaba apagado
- `JIGGLE STATUS` → modo, activación, dimensiones, posición virtual y perfil
- `JIGGLE SCREEN 1920 1080` → configura el escritorio virtual

Dimensiones permitidas: anchura 320–32767 y altura 240–32767.

## Control desde Windows

Desde PowerShell en la carpeta del proyecto:

```powershell
.\scripts\jiggler.ps1 -Status
.\scripts\jiggler.ps1 -On
.\scripts\jiggler.ps1 -Off
.\scripts\jiggler.ps1 -Slow
.\scripts\jiggler.ps1 -Medium
.\scripts\jiggler.ps1 -Fast
.\scripts\jiggler.ps1 -ScreenWidth 1920 -ScreenHeight 1080
```

El script encuentra el CDC de la aplicación por VID/PID `303A:80C2`. Se puede indicar `-Port COM6` manualmente. El modo inicial y tras reinicio es SLOW, desactivado; la selección no se guarda en memoria persistente.

## Entorno y desarrollo

- Python 3.14.5, PlatformIO Core 6.2.0
- Espressif32 8.0.0, Arduino 3.3.12, Xtensa GCC 14.2.0
- PlatformIO usa las bibliotecas USB HID/TinyUSB que incluye el framework instalado; no se añadieron dependencias HID externas.

```powershell
.\scripts\setup.ps1
.\scripts\build.ps1
.\scripts\flash.ps1
.\scripts\test.ps1
```

`flash.ps1` detecta el puerto CDC por VID/PID y utiliza el reset por touch USB a 1200 baudios para entrar al cargador ROM.

El motor puro de `src/jiggler_engine.cpp` separa la generación de movimiento de la emisión HID en `src/main.cpp`. Las pruebas automatizadas de integración (`scripts/hardware_test.py`) validan heartbeat, PING, INFO, arranque desactivado, perfiles, comandos de pantalla, límites virtuales, movimiento, cambios de modo, OFF inmediato, reenumeración CDC tras REBOOT y enumeración Windows de la interfaz Mouse. No mandan clics ni teclas.

## Validación de esta versión

- Baseline Git del firmware de diagnóstico previo: `c5b9569` (`Baseline: working ESP32-S2 diagnostic firmware`).
- Release compilado correctamente; aplicación de 357728 bytes, RAM estática 49304/327680 bytes, partición de aplicación 350156/1310720 bytes.
- Grabación en placa correcta; PlatformIO verificó los hashes de bootloader, particiones, imagen auxiliar y aplicación.
- Pruebas físicas CDC + HID completadas en COM6 (VID/PID `303A:80C2`), incluyendo reinicio y vuelta a estado apagado/SLOW.
- `pnputil` encontró `HID\VID_303A&PID_80C2&MI_00` en la clase Windows Mouse, con estado iniciado. Las pruebas también confirmaron emisión y límites de posición virtual.
- Prueba visual manual de la trayectoria del cursor en pantalla: pendiente. La enumeración HID y la ruta de movimiento se comprobaron automáticamente; la posición virtual no demuestra por sí sola el punto exacto del cursor de Windows.
