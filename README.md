<div align="center">

# ePod — Voice and Vision Restaurant Signage

**A handheld media player rebuilt into a restaurant menu you browse by talking to it. All the machine learning runs on the device.**

No cloud, and no phone needed at the table. A camera sees a guest sit down, the
screen wakes up, music starts, and the guest browses the menu by voice.

[![Platform](https://img.shields.io/badge/MCU-ESP32--S3_%2B_ESP32--WROOM-E7352C?style=for-the-badge&logo=espressif&logoColor=white)](https://espressif.com)
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

ePod started as a pocket music player: two microcontrollers, an SD card, an
OLED, a PDM microphone and a pair of earphones. This repository is that player
turned into a **digital menu board for restaurants**.

A guest sits down. A person-detection model sees them, and the screen wakes up
from an idle screen of sleeping eyes. Music starts. A line of text asks the
guest to put their earphones on, then the menu fills the screen. The guest
browses by **speaking** ("next", "back") and orders by saying a wake phrase.
There is no touchscreen and no network to depend on, because both models run on
the microcontroller itself.

---

## How it behaves

| Stage | Trigger | What happens |
|---|---|---|
| **Idle** | — | Sleeping eyes on screen. Camera watching. Nothing else runs. |
| **Guest detected** | Vision model | Camera **stops**. Eyes open, music starts, voice model starts. |
| **Welcome** | — | *"Put your earphones on and enjoy the music."* |
| **Menu** | — | Full-screen dishes. Voice control is live. |
| **Browse** | "next" / "back" | The menu swipes between plates. |
| **Order** | wake phrase | A confirmation tick, *Order placed.* |

The two models **never run at the same time**. The ESP32-S3 does not have enough
room to run a camera pipeline and a continuous audio classifier together, so the
firmware treats the switch as a handover: the camera watches an empty table, and
once a guest arrives it shuts down and the microphone takes over. You can also
turn either model off by hand from the device settings menu if one misbehaves.

---

## System architecture

```
                       ┌──────────────────────────┐
                       │  Menu web app (laptop)   │
                       │  sleeping eyes → menu →  │
                       │      order confirmed     │
                       └────────────┬─────────────┘
                                    │ HTTP over the ePod's own Wi-Fi AP
                                    │ GET /api/state · POST /api/command
                       ┌────────────┴─────────────┐
                       │   XIAO ESP32-S3 Sense    │
                       │   "AI and media core"    │
                       │  • voice keyword model   │
                       │  • person detection      │
                       │  • SD card + audio       │
                       │  • Wi-Fi AP + BLE        │
                       └────────────┬─────────────┘
                                    │ UART, line protocol
                       ┌────────────┴─────────────┐
                       │       ESP32-WROOM        │
                       │  "UI and playback core"  │
                       │  • OLED menus            │
                       │  • buttons, volume       │
                       │  • audio output          │
                       └──────────────────────────┘
                                    ⋮ BLE / Wi-Fi
                       ┌──────────────────────────┐
                       │  Android companion app   │
                       │ library sync, transfers  │
                       └──────────────────────────┘
```

The work is split across two microcontrollers on purpose. Audio breaks up if
anything stalls the loop that feeds it, so the WROOM only draws the interface
and pushes samples, and the S3 takes everything that comes in bursts: inference,
the SD card, and both radios.

<div align="center">
<img src="docs/media/wiring-diagram.png" width="80%" alt="Wiring diagram">
</div>

---

## Repository layout

```
ePod-Signage/
├── firmware/
│   ├── xiao-esp32s3/     AI and media core: models, SD, radios, web API
│   └── esp32-wroom/      UI and playback core: OLED, buttons, audio out
├── mobile/               Android companion app (Kotlin, Jetpack Compose)
├── webapp/               The signage menu page shown to guests
└── docs/media/           Renders, wiring, build photos, app screenshots
```

---

## Component documentation

Each part has its own README:

| Component | Documentation | Summary |
|---|---|---|
| 🧠 **XIAO ESP32-S3 firmware** | [`firmware/xiao-esp32s3/README.md`](firmware/xiao-esp32s3/README.md) | Voice and vision models, SD library, Wi-Fi AP, BLE, web API |
| 🎛️ **ESP32-WROOM firmware** | [`firmware/esp32-wroom/README.md`](firmware/esp32-wroom/README.md) | OLED interface, button handling, audio streaming, settings |
| 📱 **Android companion app** | [`mobile/README.md`](mobile/README.md) | Track transfer, audio encoding, BLE and Wi-Fi transport |
| 🖥️ **Signage web app** | [`webapp/README.md`](webapp/README.md) | The guest-facing menu, setup, and how to edit the dishes |

---

## Quick start

### The menu page

```bash
cd webapp
python serve.py
```

Open <http://localhost:8000> and press **F11**. With no hardware attached it
runs the whole sequence on a timed loop, so you can see how it works right away.
Press any key to take manual control.

### The firmware

Both boards build with [PlatformIO](https://platformio.org):

```bash
cd firmware/xiao-esp32s3 && pio run -t upload
cd firmware/esp32-wroom && pio run -t upload
```

### Connecting the page to the device

1. On the ePod: **Settings → Wi-Fi AP → ON**
2. Join `ePod-Music` from the laptop
3. Start `serve.py`. The status pill should read **connected**

`serve.py` also forwards `/api/*` to the device, so the browser sees one origin
instead of a cross-origin request.

---

## Hardware

<div align="center">
<img src="docs/media/bill-of-materials-and-prototype.jpg" width="46%" alt="Components laid out">
<img src="docs/media/components-power-buttons.jpg" width="46%" alt="Power, buttons and perfboard">
</div>

| Part | Role |
|---|---|
| Seeed XIAO ESP32-S3 Sense | Inference, camera, PDM microphone, SD, radios |
| ESP32-WROOM | Interface and audio playback |
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

Both models are trained in [Edge Impulse](https://edgeimpulse.com) and run on
the device.

### Voice keywords

| | |
|---|---|
| Type | Audio classification, continuous inference |
| Classes | `back`, `next`, `helloworld`, `noise`, `unknown` |
| Audio | 16 kHz PDM mono, 1-second window, 4 slices per window |
| Latency | ~250–500 ms after the word ends |

The `noise` and `unknown` classes are what make the model usable. An earlier
version had only two classes, and a softmax over two labels always adds up to
1.0, so it had no way to say "neither" and it classified silence as a command
non-stop. Adding reject classes lets the model make that call itself.

The training audio was recorded **on the device itself**, through the same PDM
microphone the model listens through later, and the firmware uses the same
capture gain. If you train on audio recorded with other hardware at another
level, the model tests well in the browser and then fails on the bench.

### Person detection

| | |
|---|---|
| Type | Object detection, single class (`person`) |
| Purpose | Notice a guest at the table, then stop |

Edge Impulse will not export two impulses from two separate projects as one
library unless you are on an Enterprise plan, so
[`firmware/xiao-esp32s3/tools/merge_impulse.py`](firmware/xiao-esp32s3/tools/merge_impulse.py)
merges a second export into the existing library instead. See that firmware's
README for where the vision path currently stands.

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

The first plan used speech recognition. It was dropped once it was clear it
would not fit the device's memory or hit the latency needed.

<div align="center">
<img src="docs/media/early-discarded-concept.jpg" width="65%" alt="Early discarded concept">
</div>

---

## Known limitations

These are worth knowing, because they set what the project can do today:

- **The vision path does not run on the device yet.** The current
  person-detection export is MobileNet SSD at 320×320. Edge Impulse itself marks
  it as unable to run under TensorFlow Lite Micro, and it compiles to about
  12 MB against a 3.3 MB flash partition. The fix is to re-train the same data
  as **FOMO** at 96×96. The merge tooling, the firmware, and the web handover
  around it are already written and tested.
- **Voice accuracy depends on the room.** Recognition is good on the training
  device in a quiet space and gets worse in noise. Recording more `noise`
  samples where the device will be used is the direct fix.
- **Vision and voice cannot run at the same time.** The firmware enforces this
  rather than leaving it to chance.
- **Wi-Fi and audio streaming do not overlap.** They share the 3.3 V rail, and
  an early revision browned out under load, so the firmware turns the radio off
  before playback starts.

---

## License

Released under the MIT License. The bundled Edge Impulse SDK and model exports
are still covered by Edge Impulse's own terms.
