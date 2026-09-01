<div align="center">

# ePod — Voice & Vision Restaurant Signage

**A handheld media player turned into an interactive restaurant menu, driven entirely by on-device machine learning.**

No cloud. No phone required at the table. A camera notices a guest sit down, the
screen wakes, music starts, and the menu is browsed by voice.

[![Platform](https://img.shields.io/badge/MCU-ESP32--S3_%2B_ESP32--WROVER-E7352C?style=for-the-badge&logo=espressif&logoColor=white)](https://espressif.com)
[![Framework](https://img.shields.io/badge/Framework-Arduino_%2F_PlatformIO-00979D?style=for-the-badge&logo=platformio&logoColor=white)](https://platformio.org)
[![ML](https://img.shields.io/badge/Edge_ML-Edge_Impulse-3B47CE?style=for-the-badge)](https://edgeimpulse.com)
[![App](https://img.shields.io/badge/Companion-Android_%2F_Kotlin-3DDC84?style=for-the-badge&logo=android&logoColor=white)](mobile/README.md)

<img src="docs/media/enclosure-render-1.png" width="85%" alt="ePod enclosure render">

</div>

---

## Table of Contents

- [What this is](#what-this-is)
- [How it behaves](#how-it-behaves)
- [System architecture](#system-architecture)
- [Repository layout](#repository-layout)
- [Component documentation](#component-documentation)
- [Quick start](#quick-start)
- [Hardware](#hardware)
- [Machine learning models](#machine-learning-models)
- [Design and build gallery](#design-and-build-gallery)
- [Known limitations](#known-limitations)
- [License](#license)

---

## What this is

ePod began as a pocket music player: two microcontrollers, an SD card, an OLED,
a PDM microphone and a pair of earphones. This repository is that player grown
into a **digital signage terminal for restaurants**.

A guest sits down. A person-detection model running on the camera notices them
and the display wakes from a sleeping-eyes idle screen. Ambient music begins.
A line of text invites the guest to put their earphones on, and the menu takes
over the screen. From there the guest browses by **speaking** — "next", "back" —
and places an order by saying a wake phrase. No touchscreen, no waiting, and no
network dependency: every model runs on the microcontroller itself.

---

## How it behaves

| Stage | Trigger | What happens |
|---|---|---|
| **Idle** | — | Sleeping eyes on screen. Camera watching. Nothing else runs. |
| **Guest detected** | Vision model | Camera **stops**. Eyes open, music starts, voice model starts. |
| **Welcome** | — | *"Put your earphones on and enjoy the music."* |
| **Menu** | — | Full-screen dishes. Voice control is live. |
| **Browse** | "next" / "back" | The menu swipes between plates. |
| **Order** | wake phrase | A confirmation tick — *Order placed.* |

The vision and voice models are **mutually exclusive by design** — never both at
once. The ESP32-S3 does not have the headroom to run a camera pipeline and a
continuous audio classifier together, so the firmware treats the transition as a
handover: the camera watches an empty table, and once a guest is present it
stands down and hands the room to the microphone. Either can also be switched
off by hand from the device's settings menu if a model misbehaves.

---

## System architecture

```
                        ┌──────────────────────────┐
                        │   Menu web app (laptop)   │
                        │  sleeping eyes → menu →   │
                        │      order confirmed      │
                        └────────────┬─────────────┘
                                     │ HTTP over the ePod's own Wi-Fi AP
                                     │ GET /api/state · POST /api/command
                        ┌────────────┴─────────────┐
                        │   XIAO ESP32-S3 Sense    │
                        │   "AI & media core"      │
                        │  • voice keyword model   │
                        │  • person detection      │
                        │  • SD card + audio       │
                        │  • Wi-Fi AP + BLE        │
                        └────────────┬─────────────┘
                                     │ UART, line protocol
                        ┌────────────┴─────────────┐
                        │      ESP32-WROVER        │
                        │   "UI & playback core"   │
                        │  • OLED menus            │
                        │  • buttons, volume       │
                        │  • audio output          │
                        └──────────────────────────┘
                                     ⋮ BLE / Wi-Fi
                        ┌──────────────────────────┐
                        │   Android companion app   │
                        │  library sync, transfers  │
                        └──────────────────────────┘
```

Splitting the work across two microcontrollers is deliberate. Audio streaming is
unforgiving of interruption, so the WROVER does nothing but drive the interface
and push samples, while the S3 absorbs everything bursty — inference, the SD
card, and both radios.

<div align="center">
<img src="docs/media/wiring-diagram.png" width="80%" alt="Wiring diagram">
</div>

---

## Repository layout

```
ePod-Signage/
├── firmware/
│   ├── xiao-esp32s3/     AI and media core — models, SD, radios, web API
│   └── esp32-wrover/     UI and playback core — OLED, buttons, audio out
├── mobile/               Android companion app (Kotlin, Jetpack Compose)
├── webapp/               The signage menu page shown to guests
└── docs/media/           Renders, wiring, build photos, app screenshots
```

---

## Component documentation

Each part of the system documents itself:

| Component | Documentation | Summary |
|---|---|---|
| 🧠 **XIAO ESP32-S3 firmware** | [`firmware/xiao-esp32s3/README.md`](firmware/xiao-esp32s3/README.md) | Voice and vision models, SD library, Wi-Fi AP, BLE, web API |
| 🎛️ **ESP32-WROVER firmware** | [`firmware/esp32-wrover/README.md`](firmware/esp32-wrover/README.md) | OLED interface, button handling, audio streaming, settings |
| 📱 **Android companion app** | [`mobile/README.md`](mobile/README.md) | Track transfer, audio encoding, BLE and Wi-Fi transport |
| 🖥️ **Signage web app** | [`webapp/README.md`](webapp/README.md) | The guest-facing menu, setup, and how to edit the dishes |

---

## Quick start

### The menu page

```bash
cd webapp
python serve.py
```

Open <http://localhost:8000> and press **F11**. It runs the full sequence on a
timed loop out of the box, so the concept demonstrates itself with no hardware
attached. Any key press hands you manual control.

### The firmware

Both boards build with [PlatformIO](https://platformio.org):

```bash
cd firmware/xiao-esp32s3 && pio run -t upload
cd firmware/esp32-wrover && pio run -t upload
```

### Connecting the page to the device

1. On the ePod: **Settings → Wi-Fi AP → ON**
2. Join `ePod-Music` from the laptop
3. Start `serve.py` — the status pill reads **connected**

`serve.py` also proxies `/api/*` to the device, which keeps the browser from
treating the ePod as a cross-origin request.

---

## Hardware

<div align="center">
<img src="docs/media/bill-of-materials-and-prototype.jpg" width="46%" alt="Components laid out">
<img src="docs/media/components-power-buttons.jpg" width="46%" alt="Power, buttons and perfboard">
</div>

| Part | Role |
|---|---|
| Seeed XIAO ESP32-S3 Sense | Inference, camera, PDM microphone, SD, radios |
| ESP32-WROVER | Interface and audio playback |
| SSD1306 OLED, 128×64 | Menus and status |
| MicroSD module | Track and recording storage |
| 3.7 V LiPo + charger board | Portable power |
| Tactile buttons, potentiometer | Navigation and volume |
| 3.5 mm output + earphones | Audio |

<div align="center">
<img src="docs/media/xiao-esp32s3-sd.jpg" width="55%" alt="XIAO ESP32-S3 with SD card">
</div>

---

## Machine learning models

Both models are trained in [Edge Impulse](https://edgeimpulse.com) and run
entirely on-device.

### Voice keywords

| | |
|---|---|
| Type | Audio classification, continuous inference |
| Classes | `back`, `next`, `helloworld`, `noise`, `unknown` |
| Audio | 16 kHz PDM mono, 1-second window, 4 slices per window |
| Latency | ~250–500 ms after the word ends |

The `noise` and `unknown` classes are what make this usable. An earlier
two-class model had no way to express "neither" — a softmax over two labels
always sums to 1.0, so silence was continuously classified as a command. Adding
explicit reject classes moved that judgement into the model where it belongs.

Training audio was captured **on the target device itself**, through the same
PDM microphone the model later listens through, and the firmware reproduces the
capture gain exactly. A model trained on audio recorded by different hardware at
a different level is a model that works in the browser and fails on the bench.

### Person detection

| | |
|---|---|
| Type | Object detection, single class (`person`) |
| Purpose | Notice a guest at the table, then stop |

Two impulses trained in two separate Edge Impulse projects cannot be exported as
one library without an Enterprise plan, so
[`firmware/xiao-esp32s3/tools/merge_impulse.py`](firmware/xiao-esp32s3/tools/merge_impulse.py)
merges a second export into the existing library by hand. See that firmware's
README for the current status and constraints of the vision path.

---

## Design and build gallery

### Enclosure

Designed in Fusion 360 and 3D printed.

<div align="center">
<img src="docs/media/enclosure-render-2.png" width="46%" alt="Enclosure render">
<img src="docs/media/enclosure-render-3.png" width="46%" alt="Enclosure render">
</div>

### Companion app

<div align="center">
<img src="docs/media/mobile/collage-1.png" width="80%" alt="App screens">
</div>

<div align="center">
<img src="docs/media/mobile/app-01.jpg" width="19%">
<img src="docs/media/mobile/app-03.jpg" width="19%">
<img src="docs/media/mobile/app-05.jpg" width="19%">
<img src="docs/media/mobile/app-07.jpg" width="19%">
<img src="docs/media/mobile/app-09.jpg" width="19%">
</div>

### An earlier direction

The first plan used a speech-recognition approach that was abandoned once it
became clear it would not fit the device's memory or latency budget.

<div align="center">
<img src="docs/media/early-discarded-concept.jpg" width="65%" alt="Early discarded concept">
</div>

---

## Known limitations

Recorded honestly, because they shape what the project can currently do:

- **The vision path is not yet running on-device.** The current person-detection
  export is MobileNet SSD at 320×320, which Edge Impulse itself marks as unable
  to run under TensorFlow Lite Micro, and which compiles to roughly 12 MB
  against a 3.3 MB flash partition. Re-training the same data as **FOMO** at
  96×96 is the intended fix; the merge tooling and the entire firmware and web
  handover around it are already in place and tested.
- **Voice accuracy varies with the room.** Recognition is good on the training
  device in a quiet space and degrades in noise. More `noise` samples captured
  in the deployment environment is the direct remedy.
- **Vision and voice cannot run simultaneously.** This is enforced in firmware
  rather than left to chance.
- **Wi-Fi and audio streaming do not overlap.** Both share the 3.3 V rail, and
  an early revision browned out under load; the firmware forces the radio off
  before playback begins.

---

## License

Released under the MIT License. The bundled Edge Impulse SDK and model exports
remain subject to Edge Impulse's own terms.
