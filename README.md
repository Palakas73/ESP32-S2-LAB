# ESP32-S2-LAB — USB HID Mouse Jiggler

Firmware para LOLIN S2 Mini (ESP32-S2FNR2, 4 MB flash y 2 MB PSRAM) que se presenta en Windows como dispositivo USB compuesto CDC + HID Mouse. El puerto CDC conserva los comandos de diagnóstico y controla el movimiento del ratón. El Jiggler arranca siempre desactivado.

Nombre de producto USB mostrado por Windows: `ESP32-S2 Generic Mouse`. El fabricante permanece como `WEMOS` y los identificadores USB de la aplicación permanecen en `303A:80C2`.

## Comportamiento

- Sin clics ni informes de teclado; los informes de ratón mantienen `buttons=0`.
- Órbita circular de radio virtual fijo de 100 px. Las dos medias vueltas usan el mismo perfil suave de velocidad para reducir la deriva causada por la aceleración de Windows.
- Máquina de estados no bloqueante. CDC se atiende en cada iteración; cada informe relativo se limita a ±2 px por eje y se emite con un intervalo mínimo de 8 ms.
- `JIGGLE OFF` vacía los incrementos pendientes e interrumpe la trayectoria inmediatamente.
- Coordenadas virtuales estimadas y acotadas por las dimensiones configuradas. Windows puede aplicar aceleración o clipping y el firmware no puede leer la posición absoluta: por eso la posición virtual no tiene por qué coincidir con el cursor real.

### Movimiento circular

El cursor virtual sigue una circunferencia de 100 px de radio alrededor del centro del escritorio configurado. Al activar, el motor vuelve a fijar el origen virtual en la circunferencia sin enviar movimiento HID; así la vuelta comienza y termina en el mismo punto físico estimado. Cada media vuelta acelera y desacelera del mismo modo; sus desplazamientos relativos son opuestos. Las vueltas se encadenan sin pausa programada: al terminar una se inicia inmediatamente la siguiente, manteniendo la misma referencia temporal. SLOW, MEDIUM y FAST completan la vuelta en 8, 6 y 4 segundos.

La posición virtual no equivale necesariamente a la posición real del cursor: Windows puede aplicar aceleración o recortar el movimiento en los bordes y el firmware no puede leer la posición absoluta ni el tamaño de la ventana activa. El radio de 100 px describe la trayectoria virtual. Para mantener el cursor real dentro de una ventana, debe comenzar con margen suficiente respecto a sus bordes; el firmware USB por sí solo no puede garantizarlo.

### Corrección del envío USB

La caída continua hacia abajo y a la izquierda procedía de `USBHID::SendReport(..., 0)`: el framework podía encolar un informe y devolver `false` al no recibir su confirmación inmediatamente. El programa interpretaba ese resultado como un envío fallido y repetía el mismo desplazamiento sin descontarlo de la cola ni respetar la cadencia prevista.

El emisor usa ahora `tud_hid_n_mouse_report`, cuyo resultado confirma la aceptación del informe sin esperar a su finalización. Cada informe aceptado se descuenta una sola vez. `JIGGLE STATUS` expone `tx_reports`, `tx_sum_x/y`, `pending_x/y` y `submitted_x/y` para comprobar la transmisión además de la trayectoria virtual.

### Modos

| Modo | Duración de una vuelta | Pausa entre vueltas |
| --- | ---: | ---: |
| SLOW | 8000 ms | 0 ms |
| MEDIUM | 6000 ms | 0 ms |
| FAST | 4000 ms | 0 ms |

Los tiempos se pueden ajustar en `src/jiggler_engine.cpp`. Cambiar el modo apagado no lo activa; cambiarlo encendido conserva la activación.

## Control autónomo con BOOT y LED

La placa LOLIN S2 Mini conecta BOOT a GPIO0 y su LED azul de estado a GPIO15. BOOT se lee como entrada con pull-up y pulsación activa en LOW. La documentación del fabricante incluye el esquema de la placa; la asignación de pines aparece también en la ficha de la placa. [WEMOS S2 mini y esquema](https://www.wemos.cc/en/latest/s2/s2_mini.html), [asignación GPIO0/GPIO15](https://circuitpython.org/board/lolin_s2_mini/).

- Pulsación corta: alterna ON/OFF. Espera 300 ms tras soltar para saber que no es el inicio de una doble pulsación.
- Doble pulsación: recorre SLOW → MEDIUM → FAST → SLOW y conserva ON/OFF.
- Antirrebote: 30 ms. La ventana y el antirrebote son constantes configurables en `src/main.cpp`.
- Al pasar a ON, por BOOT o CDC, el LED parpadea durante 4 segundos según el modo (periodos SLOW 1200 ms, MEDIUM 500 ms, FAST 180 ms). OFF cancela inmediatamente la ventana y apaga GPIO15; el LED permanece apagado mientras el Jiggler está OFF. Cambiar el modo encendido, incluido el doble clic, reinicia los 4 segundos. Un doble clic estando OFF cambia el modo y conserva el LED apagado. La indicación se inicia al reconocer la acción, después de distinguir clic corto y doble clic. El refresco usa `millis()` y no bloquea el CDC ni el motor.
- GPIO0 también es el pin de strap de arranque: BOOT mantenido mientras se reinicia sigue permitiendo entrar al cargador ROM. El firmware conserva CDC + HID y la configuración USB de Fase 1.

El LED GPIO15 de LOLIN S2 Mini es activo en HIGH; LOW lo apaga. La versión anterior tenía esa polaridad invertida. [Definición de la placa en RIOT](https://api.riot-os.org/esp32s2-wemos-mini_2include_2board_8h.html). La autoprueba `led_test` verifica parpadeo por modo, apagado a los 4000 ms, cancelación inmediata al apagar, reactivación ON, doble clic activo e inactivo, antirrebote y desbordamiento de `millis()`. `JIGGLE STATUS` añade `led_on`, `led_pin_level`, `led_window_remaining_ms`, `led_active_low` y `button_presses`. La prueba CDC comprueba el nivel físico GPIO15 en dos ciclos ON/OFF consecutivos y que el temporizador se vacía en OFF.

## Arranque y persistencia

El Jiggler siempre arranca apagado; ON nunca se guarda. El último modo válido se conserva en NVS y solo se escribe cuando cambia. Si NVS no tiene un valor válido, el modo inicial es MEDIUM. El botón BOOT y los comandos CDC operan sobre el mismo `JigglerEngine`.

## Comandos CDC

Se puede enviar una línea por USB CDC a 115200 baudios:

- `PING` → `PONG`
- `INFO` → datos de hardware y `END_INFO`
- `REBOOT` → reinicia el microcontrolador
- `JIGGLE ON` → activa con el último modo seleccionado
- `JIGGLE OFF` → desactiva y detiene movimiento pendiente
- `JIGGLE SLOW`, `JIGGLE MEDIUM`, `JIGGLE FAST` → selecciona modo sin activar si estaba apagado
- `JIGGLE STATUS` → modo, activación, dimensiones, posición virtual, radio, límite y cadencia HID, y perfil
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

El script encuentra el CDC de la aplicación por VID/PID `303A:80C2`. Se puede indicar `-Port COM6` manualmente. La selección se conserva entre reinicios; el estado siempre vuelve apagado.

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

El motor puro de `src/jiggler_engine.cpp` separa la generación de movimiento de la emisión HID en `src/main.cpp`. El reconocimiento BOOT está en `src/button_gestures.cpp` y tiene una autoprueba ejecutada en cada arranque. Las pruebas automatizadas de integración (`scripts/hardware_test.py`) validan heartbeat, PING, INFO, estado apagado, autopruebas de gestos y órbita, perfiles, radio y límites de pantalla, comandos CDC, persistencia tras dos reinicios y enumeración Windows de la interfaz Mouse. No mandan clics ni teclas.

La prueba `scripts/hid_motion_capture.py` captura mediante Windows Raw Input más de dos vueltas FAST del VID/PID de esta placa y compara los eventos con los contadores CDC. Comprueba límite por eje, ausencia de botones y rueda, radio acumulado, continuidad entre vueltas y número de eventos. Windows puede agrupar varios informes USB en un evento Raw Input: los máximos reales del emisor se verifican con `tx_max_abs_x/y`, y la suma recibida se compara con `tx_sum_x/y`. [RAWMOUSE y agrupación de eventos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse). Las autopruebas del motor verifican tres vueltas cerradas consecutivas en los tres modos. `test.ps1` ejecuta ambas pruebas; la captura necesita una sesión de escritorio Windows y termina con el Jiggler apagado en MEDIUM. Estas herramientas solo se usan para desarrollo: la placa funciona de forma autónoma sin ellas.

## Validación de esta versión

- Baseline Git del firmware de diagnóstico previo: `c5b9569` (`Baseline: working ESP32-S2 diagnostic firmware`).
- Release compilado correctamente; imagen binaria de 384288 bytes, RAM estática 49408/327680 bytes y partición de aplicación 375352/1310720 bytes.
- Grabación automática correcta desde COM6; el cargador ROM apareció en COM3 y se verificaron los hashes. No se borró toda la flash ni se tocaron eFuses.
- Pruebas físicas CDC + HID completadas en COM6 (`303A:80C2`): PING, INFO, estado apagado, autopruebas BOOT/órbita y reanclaje, tres perfiles, vuelta virtual completa de diámetro 200 px, límite de 2 px por eje cada 8 ms, OFF, persistencia FAST/MEDIUM y arranque desactivado tras reinicios.
- Windows enumeró `HID\VID_303A&PID_80C2&MI_00` en la clase Mouse. El perfil final quedó MEDIUM y apagado.
- Captura real de Windows Raw Input durante 8,3 segundos: 871 eventos y 871 informes aceptados, máximo USB real de 2 por eje, más de dos vueltas continuas con radio estable e intervalo máximo de 0,097 s entre eventos. Suma relativa final (-2, 18), coincidente con CDC: la captura termina durante una tercera vuelta parcial, por lo que no debe esperarse suma cero. Sin botones, rueda ni movimiento pendiente tras OFF.
- El usuario confirmó visualmente que el ratón se movió correctamente tras esta corrección. Windows detectó dos monitores (escritorio virtual de 5120×1152).
- Corrección del LED: autopruebas repetidas y prueba CDC real con pulsaciones BOOT del usuario. Se observaron tres ventanas completas de 4 segundos, reactivación tras expirar, clic corto y dos dobles clics (MEDIUM → FAST → SLOW), parpadeo por modo y apagado GPIO15=LOW al final de cada ventana. Cero discrepancias entre `led_on` y el nivel GPIO leído. El usuario confirmó visualmente que funciona en los tres casos.
- Ajuste posterior de OFF: la indicación queda ligada al estado activo. Se verificaron por CDC dos ciclos ON/OFF reales: ON enciende GPIO15 y abre la ventana; OFF fuerza GPIO15=LOW y `led_window_remaining_ms=0` inmediatamente. Las autopruebas verifican además que el doble clic estando OFF cambia el modo sin encender el LED. La confirmación visual previa corresponde a polaridad, parpadeo y temporizador; este ajuste se comprobó por autopruebas y lectura GPIO en la placa.
