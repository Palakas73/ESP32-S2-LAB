# ESP32-S2-LAB — USB HID Mouse Jiggler for WEMOS LOLIN S2 Mini (ESP32-S2)

Firmware developed and tested for the **WEMOS LOLIN S2 Mini**, equipped with an **ESP32-S2FNR2, 4 MB flash and 2 MB PSRAM**.

The board enumerates on Windows as a composite **USB CDC + HID mouse**. CDC provides diagnostics and motion control. The jiggler **always starts OFF**.

The tested target is the WEMOS LOLIN S2 Mini (ESP32-S2). Other ESP32 boards have not been validated with this firmware.

## Board details

| Item | WEMOS LOLIN S2 Mini (ESP32-S2) configuration |
| --- | --- |
| MCU | ESP32-S2FNR2, single core, 240 MHz |
| Flash | 4 MB |
| PSRAM | 2 MB |
| Logic voltage | 3.3 V |
| USB connection | USB-C, native USB CDC + HID mouse |
| Application VID:PID | 303A:80C2 |
| ROM bootloader VID:PID | 303A:0002 |
| BOOT button | GPIO0, pressed LOW, input pull-up |
| Onboard LED | GPIO15, blue, active HIGH; not RGB |
| USB D− / D+ | GPIO19 / GPIO20 |
| Wireless hardware | 2.4 GHz Wi-Fi; no Bluetooth Classic or BLE |
| USB manufacturer | WEMOS |
| USB product name | ESP32-S2 Generic Mouse |

Use a USB data cable between the WEMOS LOLIN S2 Mini and the target computer. This firmware provides USB and physical-button control. The board's Wi-Fi hardware is used in the separate [ESP32-Jiggler2 edition](https://github.com/Palakas73/ESP32-Jiggler2).

Official board references: [WEMOS S2 Mini documentation](https://www.wemos.cc/en/latest/s2/s2_mini.html), [V1.0.0 schematic](https://www.wemos.cc/en/latest/_static/files/sch_s2_mini_v1.0.0.pdf), and [GPIO0/GPIO15 board pinout](https://circuitpython.org/board/lolin_s2_mini/).

## Behavior

- No clicks or keyboard reports; all mouse reports keep buttons=0.
- Continuous circular motion with a fixed virtual radius of 100 pixels.
- Matching smooth speed profiles on opposite semicircles reduce drift caused by Windows pointer acceleration.
- A nonblocking state machine services CDC on every loop iteration.
- Relative HID reports are limited to ±2 units per axis and have an 8 ms minimum interval.
- JIGGLE OFF immediately stops the trajectory and clears pending increments.
- Configurable virtual desktop dimensions bound the estimated coordinates.

### Circular motion

The virtual cursor follows a circle of radius 100 around the center of the configured virtual desktop. On activation, the engine assigns its virtual starting point to the circle without sending a radial HID movement, so each complete turn begins and ends at the same estimated physical point.

Each semicircle uses matching acceleration and deceleration; its relative displacement is opposite to the other semicircle. Successive turns begin immediately with the same time reference, without a programmed pause.

SLOW, MEDIUM and FAST complete a turn in 8, 6 and 4 seconds respectively.

The virtual coordinates are estimates, not measurements of the real cursor position. Windows can apply pointer acceleration or clip movement at screen edges. Standard relative HID does not let the board read the absolute cursor position or active window dimensions.

The 100-pixel radius describes the virtual trajectory. To keep the visible cursor within a window, start it with sufficient margin from the edges; the firmware cannot guarantee containment in an arbitrary window.

### USB transmission correction

Earlier repeated down-left drift was traced to calling USBHID::SendReport with a zero wait timeout. The framework could queue a report and return false before its completion arrived. Treating that return value as a failed submission caused the same displacement to be sent repeatedly without decrementing the pending movement or respecting the intended cadence.

The emitter now uses tud_hid_n_mouse_report, which reports whether the endpoint accepted the report without waiting for completion. Each accepted report is deducted exactly once.

JIGGLE STATUS exposes tx_reports, tx_sum_x/y, pending_x/y and submitted_x/y so the actual transmission can be checked alongside the virtual trajectory.

### Speed profiles

| Mode | Full turn duration | Pause between turns |
| --- | ---: | ---: |
| SLOW | 8000 ms | 0 ms |
| MEDIUM | 6000 ms | 0 ms |
| FAST | 4000 ms | 0 ms |

Adjust these values in [src/jiggler_engine.cpp](src/jiggler_engine.cpp). Selecting a mode while OFF does not start the jiggler. Changing mode while ON keeps it running.

## Standalone BOOT button and LED control

On the WEMOS LOLIN S2 Mini (ESP32-S2), BOOT is connected to GPIO0 and the blue user LED to GPIO15. BOOT uses a pull-up and is active LOW.

- **Short press:** toggle ON/OFF. The action is recognized 300 ms after release to distinguish it from a double press.
- **Double press:** cycle SLOW → MEDIUM → FAST → SLOW while preserving ON/OFF.
- **Debounce:** 30 ms. Debounce and double-click timings are configurable constants in [src/main.cpp](src/main.cpp).
- **Turn ON:** blink the LED for four seconds using the current mode's period: SLOW 1200 ms, MEDIUM 500 ms, FAST 180 ms.
- **Turn OFF:** immediately cancel the indication window and drive GPIO15 LOW.
- **Change mode while ON:** restart the four-second indication.
- **Change mode while OFF:** leave the LED off, including when using a double press.

Indication begins when the button gesture has been recognized. The LED uses millis() and does not block CDC or the motion engine.

GPIO0 is also the ESP32-S2 bootstrapping pin: holding BOOT during reset still allows ROM downloader entry. The firmware retains composite CDC + HID operation.

The WEMOS LOLIN S2 Mini's LED is **active HIGH**; LOW turns it off. The earlier implementation had the polarity reversed. See the [RIOT WEMOS S2 Mini board definition](https://api.riot-os.org/esp32s2-wemos-mini_2include_2board_8h.html).

The LED self-test covers all blinking profiles, shutoff at 4000 ms, immediate OFF cancellation, ON reactivation, double presses while active/inactive, debounce and millis() overflow.

JIGGLE STATUS includes led_on, led_pin_level, led_window_remaining_ms, led_active_low and button_presses. CDC checks compare the physical GPIO15 level across two consecutive ON/OFF cycles and verify that OFF clears the timer.

## Boot behavior and persistence

Jiggling always starts **OFF**. The ON state is never saved.

The last valid speed is stored in NVS and written only when it changes. If there is no valid saved value, the initial speed is MEDIUM. The physical BOOT button and CDC commands act on the same JigglerEngine instance.

Saved speed survives power loss and normal firmware uploads that preserve NVS. A full flash erase removes persistent settings.

## USB CDC commands

Send a newline-terminated line at 115200 baud:

| Command | Action |
| --- | --- |
| PING | Return PONG |
| INFO | Return hardware information followed by END_INFO |
| REBOOT | Restart the microcontroller |
| JIGGLE ON | Activate using the selected mode |
| JIGGLE OFF | Stop immediately and clear pending automatic motion |
| JIGGLE SLOW | Select slow mode; preserve ON/OFF |
| JIGGLE MEDIUM | Select medium mode; preserve ON/OFF |
| JIGGLE FAST | Select fast mode; preserve ON/OFF |
| JIGGLE STATUS | Return activation, mode, screen dimensions, virtual coordinates, trajectory, HID cadence and diagnostic counters |
| JIGGLE SCREEN 1920 1080 | Configure the virtual desktop dimensions |

Allowed virtual dimensions: width 320–32767 and height 240–32767.

The heartbeat continues to report uptime, free heap, activation and mode once per second.

## Windows controls

Run PowerShell in the project directory:

~~~powershell
.\scripts\jiggler.ps1 -Status
.\scripts\jiggler.ps1 -On
.\scripts\jiggler.ps1 -Off
.\scripts\jiggler.ps1 -Slow
.\scripts\jiggler.ps1 -Medium
.\scripts\jiggler.ps1 -Fast
.\scripts\jiggler.ps1 -ScreenWidth 1920 -ScreenHeight 1080
~~~

The script automatically detects the application CDC port by VID:PID **303A:80C2**. Use -Port COM6 to select a port manually when needed; Windows COM assignments may differ.

The selected mode persists across restarts. Activation always returns to OFF.

## Build and flash the WEMOS LOLIN S2 Mini (ESP32-S2)

Validated development environment:

- Python 3.14.5 and PlatformIO Core 6.2.0.
- Espressif32 8.0.0 and Arduino-ESP32 3.3.12.
- Xtensa GCC 14.2.0.
- Framework-provided USB HID and TinyUSB libraries; no external HID library added.
- PlatformIO environment: lolin_s2_mini; build type: Release.

Python dependencies are pinned in [requirements-lock.txt](requirements-lock.txt). The included setup script expects Python 3.14 through the Windows Python launcher.

~~~powershell
git clone https://github.com/Palakas73/ESP32-S2-LAB.git
cd ESP32-S2-LAB
.\scripts\setup.ps1
.\scripts\build.ps1
.\scripts\flash.ps1
~~~

The setup script creates a local Python virtual environment and builds the firmware. The flash script detects the compatible CDC port and uses a 1200-baud USB touch to enter the ROM loader.

The application and ROM downloader may use different COM ports. BOOT/RESET intervention may be needed if automatic downloader entry fails.

## Architecture and tests

The pure motion engine in [src/jiggler_engine.cpp](src/jiggler_engine.cpp) separates trajectory generation from HID submission in [src/main.cpp](src/main.cpp). Physical button recognition is implemented in [src/button_gestures.cpp](src/button_gestures.cpp) and has a boot-time self-test. The LED state machine is in [src/status_led.cpp](src/status_led.cpp).

Run physical integration checks with the WEMOS LOLIN S2 Mini (ESP32-S2) attached:

~~~powershell
.\scripts\test.ps1
~~~

The checks in [scripts/hardware_test.py](scripts/hardware_test.py) cover heartbeat, PING, INFO, OFF at boot, gesture/orbit self-tests, speed profiles, circle radius, virtual screen limits, CDC commands, persistence across two restarts and Windows Mouse enumeration. They do not generate clicks or keyboard input.

The [Windows Raw Input capture](scripts/hid_motion_capture.py) observes more than two FAST turns from this board's VID:PID and compares the observed movement to CDC counters. It checks relative steps, absence of buttons and wheel, accumulated radius, continuity between turns and event counts.

Windows can combine several USB reports into a single Raw Input event. Actual transmitter maxima are checked with tx_max_abs_x/y, and received sums are compared with tx_sum_x/y. See [Microsoft RAWMOUSE documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse).

Motion self-tests verify three consecutive closed turns in each mode. The test script runs both CDC integration and Raw Input capture, requires an interactive Windows desktop and finishes with the jiggler OFF in MEDIUM.

These tools are for development. After flashing, the WEMOS LOLIN S2 Mini can operate autonomously using BOOT without them.

## Recorded validation for this firmware

The following results were recorded for the finalized USB-only firmware on the **WEMOS LOLIN S2 Mini (ESP32-S2)**. This documentation update does not claim a new firmware build, upload or hardware test run.

### Firmware and USB checks

- Previous working diagnostic firmware baseline: c5b9569, **Baseline: working ESP32-S2 diagnostic firmware**.
- Finalized USB-only firmware commit: ed8214f, **Finalize continuous mouse jiggler firmware**.
- Successful Release build: 384,288-byte binary, 49,408 / 327,680 bytes static RAM and 375,352 / 1,310,720 bytes application-partition usage.
- Automatic upload succeeded from application COM6 via ROM-loader COM3, with verified hashes. No full flash erase or eFuse changes were performed.
- Physical CDC + HID tests covered PING, INFO, OFF at boot, BOOT/orbit and reanchoring self-tests, all three profiles, a full virtual circle with 200-unit diameter, the 2-unit/8-ms HID limits, immediate OFF, FAST/MEDIUM persistence and OFF after reboot.
- Windows enumerated HID\VID_303A&PID_80C2&MI_00 in the Mouse class. The final test state was OFF in MEDIUM.

COM numbers describe the development computer and are not fixed board requirements.

### Real Windows Raw Input capture

An 8.3-second capture observed 871 events and 871 accepted reports, with a maximum actual USB delta of 2 per axis. It covered more than two continuous turns with a stable integrated radius and a maximum observed event gap of 0.097 seconds.

The final relative sum was (−2, 18), matching the CDC counters. Capture ended during a partial third turn, so a zero final sum was not expected. No buttons, wheel activity or pending motion remained after OFF.

The user visually confirmed correct automatic cursor movement after the transmission fix. The development computer had two monitors forming a 5120×1152 virtual desktop.

### LED validation

Repeated self-tests and real BOOT presses verified three complete four-second indication windows, reactivation after expiry, a short press and two double presses progressing MEDIUM → FAST → SLOW.

GPIO15 was LOW after each indication window. No discrepancy was found between led_on and the physical GPIO reading. The user visually confirmed the behavior in all three cases.

The subsequent OFF adjustment linked indication to activation. Two real CDC ON/OFF cycles confirmed that ON lights GPIO15 and starts the window, while OFF immediately drives GPIO15 LOW and sets led_window_remaining_ms=0. Self-tests also verify that a double press while OFF changes mode without lighting the LED.

The earlier visual confirmation covered polarity, blinking and the timer. The later OFF adjustment was checked by self-tests and physical GPIO readings on the board.

## Related project

[ESP32-Jiggler2](https://github.com/Palakas73/ESP32-Jiggler2) is the independent Wi-Fi edition for the same **WEMOS LOLIN S2 Mini (ESP32-S2)**, adding a captive portal, a phone trackpad, persistent WPA2 settings, acceleration and drag selection.
