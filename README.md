# Songa Watch

Smart watch firmware for the **Waveshare ESP32-S3-Touch-AMOLED-2.06** â€” a
410Ã—502 AMOLED, capacitive touch, 6-axis IMU, PMU, RTC, microphones, speaker
and a microSD slot, driven by LVGL 9 on ESP-IDF 5.3.

27 screens: the watch face and dashboard, sensor and battery detail, Wi-Fi,
Bluetooth, audio, storage, camera and on-device vision, the full settings
tree in English and Japanese, and a five-screen OBD2 car-diagnostics section.

---

## Building

The build is **fully offline**. Every third-party dependency is committed
under `components/`, there is no `idf_component.yml` anywhere in the tree,
and the top-level `CMakeLists.txt` sets `IDF_COMPONENT_MANAGER=0`, so
nothing is ever fetched from the network.

```bash
. $IDF_PATH/export.sh          # ESP-IDF v5.3
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

If `idf.py build` fails with `ninja: error: failed recompaction: Permission
denied` before compiling anything, that is the host, not the project -
something is holding `build/build.ninja` open (an on-access virus scanner is
the usual culprit, an editor indexing `build/` the next most likely). Delete
`build/` and rebuild; if it recurs, exclude the build directory from scanning.

`sdkconfig.defaults` is only read when `sdkconfig` does not yet exist. After
changing it, delete `sdkconfig` - or make the same change in `menuconfig` -
or you will keep building the old configuration.

`idf.py menuconfig` â†’ **Songa Watch hardware** and **Songa Watch services**
hold the project's own options (camera pins, idle timings, OTA server,
firmware version).

### Vendored dependencies

| Component | Version | Why |
| --- | --- | --- |
| `lvgl` | 9.5.0 | UI toolkit |
| `esp32_s3_touch_amoled_2_06` | 2.0.0 | Waveshare BSP: panel, touch, I2S, SD |
| `esp_lcd_sh8601` | 2.0.0 | QSPI driver for the panel's CO5300 |
| `esp_lcd_touch`, `esp_lcd_touch_ft5x06` | 1.2.1 / 1.1.1 | FT3168 touch |
| `esp_lvgl_port` | 2.9.0 | LVGL â†” esp_lcd glue |
| `esp_codec_dev` | 1.5.11 | ES8311 speaker, ES7210 microphone |
| `esp_lcd_panel_io_additions`, `esp_io_expander`, `cmake_utilities` | â€” | BSP dependencies |

About 75 MB. LVGL's `tests/`, `docs/`, the built-in font sources and the
non-ESP platform ports were removed â€” none are referenced by
`env_support/cmake/esp.cmake`. The registry's `espressif/usb` component was
**not** vendored: it requires IDF â‰¥ 5.5.3 and would shadow the copy built
into IDF 5.3, which is what actually satisfies the BSP's reference to it.

No `idf_component.yml` survives anywhere in the tree. Two components
(`esp_lcd_sh8601`, `esp_lcd_panel_io_additions`) read their own manifest at
configure time for a version banner; those versions are pinned in their
`CMakeLists.txt` instead. `esp_lcd_touch_ft5x06` gained its `esp_lcd_touch`
dependency the same way. Leaving the manifests in place would have been
harmless today but is a standing trap: the moment anything re-enables the
component manager, their dependency lists would send the build to the
registry.

---

## Architecture

```
main/                       boot order and wiring
components/
  watch_hal/                drivers the BSP does not cover
    axp2101.c                 PMU: fuel gauge, charger, power key, power off
    qmi8658.c                 IMU: motion, hardware pedometer, low-power mode
    pcf85063.c                RTC: survives a full power cut
    cam_mega.c                Arducam Mega SPI camera (add-on)
  watch_svc/                services - no LVGL calls anywhere in here
    svc_event.c               the event bus every service posts to
    svc_settings.c            NVS-backed settings
    svc_i18n.c                English and Japanese string tables
    svc_power.c               idle policy, DFS, light sleep, shutdown
    svc_sensors.c             IMU sampling and step history
    svc_time.c                RTC, SNTP, timezone, formatting
    svc_wifi.c / svc_ble.c    radios
    svc_obd2.c                ELM327 over BLE
    svc_storage.c             SD card and SPIFFS
    svc_audio.c               WAV record and playback
    svc_camera.c              preview and stills
    svc_vision.c              object detection (pluggable backend)
    svc_ota.c                 HTTPS OTA with rollback
  watch_ui/                 LVGL - all 27 screens
    ui_theme.c                colours, fonts, card styles
    ui_nav.c                  screen registry, back stack, lifecycle
    ui_widgets.c              header, cards, rows, gauges
    screens/                  the screens themselves
```

**Threading.** Services run on their own tasks and never touch LVGL. They
post to the event bus; the UI subscribes and takes the LVGL lock inside its
handler. Two helpers make this safe:

- `ui_screen_subscribe()` unsubscribes automatically when the screen is
  deleted.
- `ui_screen_add_timer()` deletes the timer with the screen.

Every event handler also re-checks `ui_nav_is_current()` before touching a
widget, because the user may have navigated away between the event being
posted and the handler running.

Anything that blocks â€” an ELM327 exchange, a BLE connect â€” runs on a worker
task, never in a click handler.

---

## Battery

The idle policy in `svc_power.c` is where the runtime is won:

| State | Trigger | What happens |
| --- | --- | --- |
| Active | touch | full brightness, IMU at 125 Hz |
| Dimmed | `idle_dim_sec` (10 s) | brightness Ã· 4 |
| Asleep | `idle_off_sec` (30 s) | panel register `0x51 = 0`, LVGL task stopped, IMU to accel-only at 3 Hz with its **hardware pedometer still counting**, SoC in automatic light sleep |

Supporting choices:

- **True black backgrounds.** On an AMOLED an unlit pixel draws no current,
  so the dark areas of every screen are free. This is why `UI_COLOR_BG` is
  `0x000000` and not a dark grey.
- **The SoC leaves light sleep on an interrupt, not a timer.** The touch
  controller's INT line (GPIO 38) is armed as a wake source, so an idle
  watch is not woken just to discover nothing happened. Because
  `lvgl_port_stop()` disables LVGL's timers â€” and therefore its reading of
  the touch device â€” the power task polls that same line at 100 ms *while
  asleep only*, and that is what turns the screen back on. The PWR button
  arrives separately, as an AXP2101 interrupt.
- **Radios off by default.** Wi-Fi and BLE only start if the user left them
  on, and Wi-Fi uses `esp_wifi_stop()` rather than idling associated.
- **Wi-Fi modem sleep** (`WIFI_PS_MIN_MODEM`) whenever it is connected.
- **Bounded reconnects.** Four attempts, then stop â€” a station stuck in a
  connect loop is one of the fastest ways to flatten a cell.
- **The camera and detector stop the instant their screen closes.**

`CONFIG_ESP_SLEEP_POWER_DOWN_FLASH` is deliberately *off*: it depends on
`!SPIRAM`, and this board has 8 MB of octal PSRAM.

---

## Hardware notes

Pins claimed by the BSP (do not reuse): I2C 14/15, QSPI 12/11/4/5/6/7 + RST
8, touch RST 9 / INT 38, I2S 16/41/45/40/42 + PA 46, SDMMC 2/1/3. Also
unavailable: GPIO 0 (BOOT), 19/20 (USB), 26â€“37 (flash + octal PSRAM), 43/44
(console).

**The PWR button is wired to the AXP2101, not to the SoC.** Presses arrive
as PMU interrupts, never as a GPIO edge â€” short press toggles the display,
long press powers off.

### Where the design and the board disagree

The design set shows three things this board does not have. Each is handled
by measuring something real rather than printing a plausible number:

| Design element | Reality | What ships |
| --- | --- | --- |
| Heart rate (72 BPM) | No optical sensor on this board, and none in scope | The dashboard tile carries the **step count** from the IMU's hardware pedometer |
| Weather (22 Â°C, Cloudy) | No weather source without an internet API key | **Ambient temperature** from the QMI8658 die sensor, labelled as temperature |
| Battery current (182 mA) | The AXP2101 has no battery-current ADC | The row shows charge **direction**; voltage and percentage are real |
| Camera | No camera and no free DVP bus â€” a parallel sensor needs 13 pins, this board has 8 left | An **Arducam Mega** (3 MP / 5 MP) on SPI, 4 wires. See below |

---

## Camera (add-on)

Supported module: **Arducam Mega SPI**, 3 MP or 5 MP. It was chosen because
it needs only four wires and does its own JPEG encoding, which is the only
shape of camera this pin-starved board can carry.

Default wiring, all adjustable in menuconfig:

| Signal | GPIO |
| --- | --- |
| SCK | 10 |
| MOSI | 13 |
| MISO | 18 |
| CS | 21 |
| VCC / GND | 3V3 / GND |

> **Check these against your board before wiring.** They were chosen from
> the GPIOs the BSP does not claim; confirm they are actually broken out on
> your revision.

With no module attached, `svc_camera_init()` returns `ESP_ERR_NOT_FOUND` and
the Camera, AI Vision and Detection screens say so instead of showing a
frozen frame.

Preview frames are pulled as **RGB565, not JPEG** â€” the module can emit
either, and taking RGB565 keeps a JPEG decoder out of the dependency set
entirely. The cost is bandwidth: 320Ã—240Ã—2 over SPI at 8 MHz is about 6 fps.
Stills are JPEG, written straight to a file without ever being decoded.

## Object detection

`svc_vision` ships a **motion-and-blob detector**: frame differencing on the
luma plane, connected-component grouping, confidence from region density and
change energy. It runs in real time on the S3 and genuinely detects things â€”
but it localises motion, it does not classify, so every object is reported
as `SVC_VISION_CLASS_MOTION`.

A real classifier (a quantised YOLO through ESP-DL) drops in behind
`svc_vision_set_backend()` with no screen changes: the detection screens
already render whatever class names the backend reports. That model is not
bundled because it cannot be fetched during an offline build.

---

## OBD2

**The ESP32-S3 radio has no Bluetooth Classic.** The common blue ELM327
dongles and "OBDII Interface" boxes are Bluetooth Classic and *cannot* pair
with this chip at all. What works is a **BLE** adapter exposing a
Nordic-UART-style write/notify pair â€” the ones advertising as `OBDBLE`,
`IOS-Vlink`, `VEEPEAK` and similar. Three GATT profiles are probed:
`FFF0/FFF2/FFF1`, `FFE0/FFE1`, `18F0/2AF1/2AF0`.

Implemented: adapter scan and auto-pick, the ELM327 init sequence
(`ATZ ATE0 ATL0 ATS0 ATH0 ATSP0`), live PIDs 04/05/0C/0D/0F/11/2F plus
`ATRV`, stored and pending DTCs (modes 03/07) decoded per SAE J2012, clearing
(mode 04), freeze frames (mode 02) and readiness monitors (PID 01).

ABS, SRS and transmission status are **not** shown as live values: those
live on manufacturer-specific buses that generic OBD2 cannot reach. The
Vehicle Status screen shows the readiness monitors that genuinely are
available and says so.

---

## Languages

English and Japanese, switchable without a reboot â€” `WATCH_EV_LANG_CHANGED`
rebuilds the current screen with the new strings and the right font.

Japanese renders with LVGL's bundled `lv_font_source_han_sans_sc_16_cjk`,
which is a **fixed 1373-glyph subset**, not a complete CJK face. A character
outside it draws as nothing, and the build gives no warning â€” so this is
checked mechanically:

```bash
pwsh -File tools/check_i18n_font.ps1
```

It scans every non-ASCII string literal in the firmware's own sources
against the font's actual glyph list. Run it after touching any Japanese
text. See `docs/i18n.md` for the substitutions the subset forced.

---

## Tooling

| Script | What it does |
| --- | --- |
| `tools/check_i18n_font.ps1` | Every non-ASCII character used is in the font |
| `tools/check_consistency.ps1` | Screens declared = defined, i18n ids valid and present in both tables, `CONFIG_` symbols defined, braces balanced |
| `tools/check_requires.ps1` | Every `#include` is covered by a declared `REQUIRES`/`PRIV_REQUIRES` |

All three run without ESP-IDF installed. `check_requires.ps1` is the one worth
running before every build: with the component manager off, nothing derives
dependencies from a manifest, so a missing `REQUIRES` entry configures cleanly
and then fails deep into compilation with `No such file or directory`, one
header at a time. Set `IDF_PATH` and it checks ESP-IDF's own components too;
without it, headers that IDF provides are skipped rather than guessed at.

---

## Status

Written against the vendor BSP, the datasheets and the register maps from
Waveshare's own example code; the LVGL 9.5 API surface was checked against
the vendored source symbol by symbol.

**CMake configure completes on ESP-IDF v5.3.5** for `esp32s3`: all components
resolve, the offline build produces no registry traffic, and `sdkconfig` is
generated without deprecation warnings.

**It has not been compiled or run on hardware.** Configure is a long way
short of a link, so expect to work through compiler errors on the first real
build. The parts most worth testing first are the ones no static check can
reach: the Arducam SPI timing, the ELM327 exchange against a real adapter,
and the light-sleep wake path.
