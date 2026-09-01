# ESP32-WROOM — UI and Playback Core

Everything the guest sees and hears. The WROOM owns the OLED, the buttons and
the audio output, and nothing else, so that playback is never interrupted.

[← back to the project README](../../README.md)

---

## Table of Contents

- [Responsibilities](#responsibilities)
- [Building](#building)
- [Screens](#screens)
- [Buttons](#buttons)
- [Settings menu](#settings-menu)
- [Link protocol](#link-protocol)
- [Why two microcontrollers](#why-two-microcontrollers)

---

## Responsibilities

| Area | Detail |
|---|---|
| Display | SSD1306 128×64 OLED: menus, playback, settings |
| Input | Three buttons, tap and hold, with volume repeat |
| Audio out | Streams raw PCM received over UART |
| Settings | Toggles for BLE, Wi-Fi, Voice AI and Vision AI |

It holds no models, no SD card and no radios. Those belong to the
[XIAO ESP32-S3](../xiao-esp32s3/README.md).

---

## Building

```bash
pio run              # build
pio run -t upload    # flash
pio device monitor   # 115200 baud
```

---

## Screens

| Screen | Purpose |
|---|---|
| `SPLASH` | boot |
| `HOME` | Music · Recordings · Record · Settings |
| `LIST` | the library, one track per row |
| `PLAY` | now playing, elapsed time, volume |
| `REC` | recording, with a live level bar |
| `SETTINGS` | radios and AI models |

---

## Buttons

| Screen | Left / Right | Middle | Hold |
|---|---|---|---|
| Home | move selection | open | — |
| List | move selection | play | back to home |
| Play | previous / next | pause | stop |
| Record | — | stop | stop |
| Settings | move selection | toggle | back to home |

Holding left or right changes the volume, repeating while you hold. After a long
press, the tap action does not also fire when you let go.

---

## Settings menu

Five rows at 9 px instead of four at 10. A 64 px panel has no spare lines, so
the detail footer was cut to one line to fit the Vision entry.

```
> BLE Bluetooth        OFF
  Wi-Fi AP             ON
  Voice AI             ON
  Vision AI            OFF
  Back
P:epodmusicpass
```

Selecting a row sends the request to the S3 and stays on the screen. The S3
decides what is allowed (vision and voice cannot both be on) and replies with a
fresh `SETTINGS` line, so the `ON`/`OFF` label shows what actually happened
rather than what was asked for.

The footer shows whichever fact is most useful at the time: the Wi-Fi password
while the access point is up, otherwise what the active model is doing.

---

## Link protocol

Newline-terminated ASCII over UART at 115200. The WROOM sends button intent; the
S3 sends state.

**To the S3**

```
BTN NEXT · BTN PREV · BTN PLAY · BTN STOP
BTN SRC 0 · BTN SRC 1 · BTN REC · BTN RECSTOP
BTN BLE_ON · BTN WIFI_ON · BTN AUDIO_ON · BTN VISION_ON   (and _OFF forms)
BTN SETTINGS_REQ
```

**From the S3**

```
SETTINGS <ble> <wifi> <ssid> <pass> <ip> <voice> <vision>
INFO <tracks> <seconds> <cardMB>
SRC <source> <music> <recordings>
PROGRESS <percent> <text>
REC <seconds> <peak>
MSG <text>
```

The `SETTINGS` line is split into fields before parsing. An earlier version
walked it with nested `strchr` calls and stopped after the IP address, so the
trailing flags were never read: the menu showed `OFF` permanently, and the
unterminated IP field swallowed the next value.

---

## Why two microcontrollers

Audio breaks up if anything stalls the loop that feeds it. Every job that can
stall unpredictably (SD seeks, radio traffic, inference) lives on the S3, which
leaves the WROOM a steady loop: read the UART, push samples, draw the screen.

The radios matter electrically too. Both boards share the 3.3 V rail, and an
early revision browned out when Wi-Fi transmitted during playback. The firmware
now turns the radio off before playback starts.
