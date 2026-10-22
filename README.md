# ESP32-S2 Mini Lab

Firmware de diagnóstico para LOLIN S2 Mini (ESP32-S2FNR2, 4 MB flash y PSRAM). PlatformIO se instala en `.venv`, aislado del Python global.

## Entorno

- Windows 11 x64, Python 3.14.5
- PlatformIO Core 6.2.0, esptool CLI 5.6.0, pyserial 3.5 (Python packages fijados en `requirements-lock.txt`)
- Plataforma Espressif32 8.0.0, framework Arduino 3.3.12, Xtensa GCC 14.2.0 y esptool 5.4.0 de PlatformIO
- USB OTG/TinyUSB CDC al arranque; PSRAM QSPI

## Comandos PowerShell

```powershell
.\scripts\setup.ps1
.\scripts\build.ps1
.\scripts\flash.ps1
.\scripts\monitor.ps1
.\scripts\test.ps1
.\scripts\diagnose.ps1
```

flash.ps1 busca USB serial por VID y no fija el número COM. También admite `-Port COMx`.

El proyecto fija la plataforma Espressif32 en 8.0.0 para repetir la misma combinación de toolchain y framework.

Comandos de firmware: PING responde PONG; INFO devuelve los datos hasta END_INFO; REBOOT reinicia el MCU; HEARTBEAT aparece aproximadamente cada segundo.

El script de prueba ordena un reinicio con `REBOOT` y verifica la reenumeración USB, heartbeat y reinicio del uptime.

## Informe de ejecución (22/10/2026)

- Compilación Release: correcta; binario ESP32-S2 final de 325504 bytes.
- Detección USB: COM3, VID/PID 303A:0002.
- Identificación física por esptool: ESP32-S2FNR2 revisión 1.0, flash 4 MB, PSRAM 2 MB, USB-OTG. MAC leída por esptool: 7c:e8:b1:49:64:de.
- Grabación PlatformIO: bootloader, tabla de particiones, imagen auxiliar y aplicación escritos; los cuatro hashes se verificaron. No se usó `erase_flash` ni se modificaron eFuses.
- La primera secuencia de carga necesitó un reset manual. Después, el touch USB desde CDC de la aplicación permitió que `flash.ps1` grabara automáticamente; PlatformIO terminó con `SUCCESS` y la placa cambió de COM5 a COM3 en modo bootloader y volvió a COM5.
- Pruebas físicas pasadas en COM5: enumeración USB (VID/PID 303A:80C2), heartbeat, `PING` -> `PONG`, `INFO` con modelo ESP32-S2, revisión 100, 1 core, 240 MHz, flash 4194304 bytes y PSRAM 2097152 bytes.
- Reconexión tras reinicio pasada: `REBOOT` provocó la reenumeración y el heartbeat volvió; el uptime pasó de 34030 ms a 1002 ms.
- Binario final: 325504 bytes; RAM estática 48912/327680 bytes y flash 318968/1310720 bytes en la partición de aplicación.
- El entorno aislado y el firmware se verificaron desde esta sesión; el sandbox del agente limitó algunos comandos no elevados que usan `C:\Users\Admin\.platformio`, por lo que las operaciones de PlatformIO sobre ese directorio se ejecutaron con autorización.
