# XIAO ESP32-S3 — AI and Media Core

The brain of the ePod. It holds both machine-learning models, the SD card
library, both radios, and the HTTP API the signage web app talks to.

[← back to the project README](../../README.md)

---

## Table of Contents

- [Responsibilities](#responsibilities)
- [Building](#building)
- [Pin map](#pin-map)
- [The two models](#the-two-models)
- [Mode arbitration](#mode-arbitration)
- [Web API](#web-api)
- [Serial console](#serial-console)
- [Merging a second impulse](#merging-a-second-impulse)
- [Vision status](#vision-status)
- [Troubleshooting](#troubleshooting)

---

## Responsibilities

| Area | Detail |
|---|---|
| Inference | Voice keyword spotting; person detection |
| Storage | SD card for the music library and recordings, hot-swappable |
| Audio in | PDM microphone (recording and inference) |
| Radios | Wi-Fi Soft-AP (uploads, signage API) and BLE (control, telemetry) |
| Link | UART line protocol to the WROOM |

Audio *output* and the user interface belong to the WROOM. Everything that comes
in bursts lives here, so an SD seek, a radio or an inference pass never
interrupts playback.

---

## Building

```bash
pio run              # build
pio run -t upload    # flash
pio device monitor   # 115200 baud
```

Two build flags matter and should not be removed:

```ini
-DEIDSP_QUANTIZE_FILTERBANK=0   ; required by Edge Impulse on ESP32
-DBOARD_HAS_PSRAM               ; 8 MB OPI PSRAM
```

`EIDSP_QUANTIZE_FILTERBANK` defaults to `1` inside the SDK. At that setting the
MFE filterbank is computed in quantized int8 and the features come out useless:
the DSP and the network both run and report timings, but every class returns
`0.00`. It has to be a **build flag**, not a `#define` in `main.cpp`, because the
library's own `.cpp` files are separate translation units that would never see
it.

---

## Pin map

| Signal | GPIO |
|---|---|
| PDM microphone clock | 42 |
| PDM microphone data | 41 |
| SD SCK / MISO / MOSI | 7 / 8 / 9 |
| SD CS | 21 (falls back to 3, 2, 4) |
| UART TX → WROOM RX2 | 43 |
| UART RX ← WROOM TX2 | 44 |

---

## The two models

### Voice keywords

Continuous inference on a sliding one-second window, four slices per second.

```
Voice: rms 2936 dsp 22ms nn 1421us pp 20us | back 89 helloworld 0 next 0 noise 8 unknown 3 | top back lead 82 -> counting
Voice: BACK  (0.99) -> previous track
```

Every inference prints one line: the level of the slice, each class score, and
which gate a word was rejected at (`below threshold`, `too close to call`,
`not a command`, `in cooldown`, `agree n/N`). Type `VOICE` on the console to
turn the printing on and off.

Three gates sit on top of the model output, all `#define`s together at the top
of the voice section:

| Gate | Default | Purpose |
|---|---|---|
| `VOICE_CONF_THRESHOLD` | 0.60 | minimum winning confidence |
| `VOICE_MARGIN` | 0.15 | how far the winner must lead the runner-up |
| `VOICE_AGREE_SLICES` | 1 | consecutive winning slices |
| `VOICE_COOLDOWN_MS` | 1000 | gap between accepted commands |

They act on the classifier's **output** only, never on the audio stream.
`run_classifier_continuous()` keeps a rolling window of MFCCs and drops exactly
one slice per call, so it has to be fed every slice in order. Skipping quiet
slices splices audio from different moments into the window and ruins the
features a spoken word depends on.

### Microphone gain

The voice path matches the conditions the training set was recorded under:
hardware `amplify_num` at the driver default of 1, then a ×4 software shift. The
recorder keeps its own higher gain. Both are applied by `micConfigure(rate,
amplify)`, which re-clocks and re-slots the single PDM channel on the way in and
out of each mode.

---

## Mode arbitration

`setVoiceMode()` and `setVisionMode()` are the only ways into either mode, and
**each one switches the other off first**. Turning vision on also stops
playback: a 320×320 frame plus inference leaves no room to stream audio off the
card, and the table is empty at that point anyway.

```
   camera watching an empty table
              │  person detected
              ▼
   onGuestDetected()
     ├─ setVisionMode(false)      camera stops
     ├─ webEmit("person")         eyes open on the web app
     ├─ requestPlay()             music starts
     └─ setVoiceMode(true)        menu keywords take over
```

`onGuestGone()` reverses it. Both modes also appear in the WROOM's settings
menu, so either can be turned off by hand.

---

## Web API

Served over the Soft-AP once Wi-Fi is on. CORS headers are sent, so you can point
a browser straight at the device if the laptop has no Python.

| Endpoint | Returns |
|---|---|
| `GET /api/state` | `{seq, event, person, idle, vision, voice, playing, tracks}` |
| `POST /api/command` | one of `vision_on` `vision_off` `voice_on` `voice_off` `wake` `sleep` |
| `GET /status` | one-line library summary |
| `POST /upload` | track upload |

`seq` increases on every new event. The web app acts whenever it changes, so an
event is never missed between two polls and never repeated after a page reload.
`POST wake` runs the whole guest-arrival sequence by hand, which is how the flow
is tested without a camera.

**Access point:** `ePod-Music` / `epodmusicpass` at `192.168.4.1`.

---

## Serial console

115200 baud. `HELP` lists everything; the useful ones here:

| Command | Effect |
|---|---|
| `VOICE` | turn per-inference reporting on/off, print the model's class list |
| `VOICEDUMP` | run the next inference with the SDK's own debug output |
| `SDINFO` | mount state, CS pin, free heap, largest contiguous block |
| `INFO` | library, microphone, radio status |
| `WHY` | last reset reason and the stage it died in |

---

## Merging a second impulse

Edge Impulse will not export two impulses from two separate projects as one
library unless you are on an Enterprise plan. `tools/merge_impulse.py` does it
instead:

```bash
python tools/merge_impulse.py "path/to/second-export"
```

It works because every symbol Edge Impulse generates is already namespaced by
project id, and because the impulse struct carries literal values rather than
`EI_CLASSIFIER_*` macros. That means `model_metadata.h`, the one file that
really does collide, is not needed from the second export at all.

The script:

1. copies the second model's compiled graph
2. rewrites `trained_model_ops_define.h` as the **intersection** of both DISABLE
   sets. Those files compile kernels *out*, so taking the union would remove a
   kernel the other model needs, and that shows up at runtime as a model that
   loads and then silently returns nothing
3. appends the second impulse, keeping exactly one `ei_default_impulse`
4. refuses any export whose `LARGEST_ARENA_SIZE` is 0

Run the merged impulse explicitly:

```cpp
run_classifier(&impulse_handle_<projectid>_1, &signal, &result, false);
```

If the link fails on a `Register_*` symbol, the two exports were generated
against different TensorFlow Lite Micro versions. Add a forwarder next to the one
in `src/tflite-model/ei_merge_shim.cpp`.

---

## Vision status

The person-detection path **does not run on the device yet**. The merge itself
succeeds and links, but the available export is MobileNet SSD at 320×320:

```c
#define EI_CLASSIFIER_OBJECT_DETECTION_LAST_LAYER  EI_CLASSIFIER_LAST_LAYER_SSD
#define EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE    0
```

```
#error "This model cannot run under TensorFlow Lite Micro
        (EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE is 0)."
```

It compiles to about **12.8 MB against a 3.34 MB partition**. That architecture
is meant for a Raspberry Pi or a phone, not an MCU.

**The fix is in Edge Impulse Studio:** change the object-detection block to
**FOMO** at 96×96 grayscale, which is usually 50–100 KB, and re-export. The
existing training images can be reused; only the learn block changes. Everything
after that point (`onGuestDetected()`, mode arbitration, the web handover) is
already built and tested.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| Every class returns `0.00`, `nn` non-zero | `EIDSP_QUANTIZE_FILTERBANK` not 0, or a performance-calibration block in the impulse holding back output until it detects an event |
| Voice fires constantly on silence | the model has no `noise`/`unknown` class |
| `Voice: PDM init failed` | PDM receive exists only on I2S0 on the ESP32-S3 |
| SD stops mounting after a re-insert | heap: check `SDINFO` for the largest contiguous block |
| Garbled serial lines | two cores printing at once; build the line and write it in one call |
| Link fails on 3 random kernel objects | a dropped file in the parallel build; run `pio run` again |
