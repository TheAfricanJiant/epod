# epod — Hardware Interconnect & Wiring Documentation

| | |
|---|---|
| **Project** | epod |
| **Scope** | Dual-controller audio + storage + display + input subsystem |
| **Primary MCU** | Seeed XIAO ESP32-S3 (on ReSpeaker Lite) |
| **Secondary MCU** | Seeed XIAO ESP32-C3 |
| **Status** | Verified — all connections below are tested and working |

---

## Contents

1. [Overview](#1-overview)
2. [Primary Controller: ESP32-S3 on ReSpeaker Lite](#2-primary-controller-esp32-s3-on-respeaker-lite)
3. [Design Rationale: Why a Second Controller](#3-design-rationale-why-a-second-controller)
4. [System Architecture](#4-system-architecture)
5. [Connection Tables](#5-connection-tables)
   - 5.1 [S3 ↔ C3 — Power & UART Link](#51-s3--c3--power--uart-link)
   - 5.2 [S3 ↔ Button Ladder (Analog Input)](#52-s3--button-ladder-analog-input)
   - 5.3 [C3 ↔ OLED Display (I2C)](#53-c3--oled-display-i2c)
   - 5.4 [C3 ↔ microSD Module (SPI)](#54-c3--microsd-module-spi)
   - 5.5 [Full Pin Budget Summary](#55-full-pin-budget-summary)
6. [Button Ladder Calibration](#6-button-ladder-calibration)
7. [Known Hardware Considerations](#7-known-hardware-considerations)
8. [Bill of Materials & Cost Analysis](#8-bill-of-materials--cost-analysis)

---

## 1. Overview

epod is built around a **Seeed XIAO ESP32-S3**, pre-mounted on a **Seeed ReSpeaker
Lite** carrier board for dual-microphone voice capture (XMOS XU316 audio
front-end). The S3 handles audio and sensor input. Because the carrier board
consumes nearly all of the S3's exposed GPIO for its own audio pipeline, a
**second microcontroller — a XIAO ESP32-C3** — is used as a dedicated
peripheral controller, linked to the S3 over UART, to drive a microSD card
and an OLED status display.

## 2. Primary Controller: ESP32-S3 on ReSpeaker Lite

The ReSpeaker Lite board occupies the following S3 pins for its onboard I2S
audio path and I2C control bus to the XMOS XU316 / audio codec:

| S3 Pin | GPIO | Function (owned by ReSpeaker Lite) |
|---|---|---|
| D4 | GPIO5 | I2C SDA (XU316 + codec control) |
| D5 | GPIO6 | I2C SCL |
| D6 | GPIO43 | I2S DOUT (speaker) |
| D7 | GPIO44 | I2S DIN (mic) |
| D8 | GPIO7 | I2S LRCLK |
| D9 | GPIO8 | I2S BCLK |
| D10 | GPIO9 | I2S MCLK |

**Pins available for custom use:** only **D0 (GPIO1), D1 (GPIO2), D2 (GPIO3),
D3 (GPIO4)** — 4 of the S3's 11 GPIO remain free.

## 3. Design Rationale: Why a Second Controller

The project requires an SD card (SPI: CS, SCK, MOSI, MISO — 4 pins) **and**
an OLED display (I2C: SDA, SCL — 2 pins) — **6 pins total**. Only 4 pins are
free on the S3, so both peripherals cannot be driven directly from it.

A second XIAO ESP32-C3 was added as a dedicated I/O expander: a 2-wire UART
link (TX/RX) connects the two boards, consuming only 2 of the S3's 4 free
pins, while the C3 — which has a full 11-pin GPIO budget of its own — takes
on the SD card and OLED entirely. This leaves the S3 with spare pins for
direct sensor input (the button ladder), rather than routing every
peripheral through a single pin-starved controller.

## 4. System Architecture

```
 ┌─────────────────────────────┐        UART        ┌───────────────────────┐
 │   XIAO ESP32-S3              │◄───────────────────►│   XIAO ESP32-C3        │
 │   (on ReSpeaker Lite)        │   TX/RX, 5V, GND     │                        │
 │                               │                      │                        │
 │  • Dual-mic audio (XU316)    │                      │  • microSD card (SPI)  │
 │  • 3-button analog ladder    │                      │  • OLED display (I2C)  │
 └─────────────────────────────┘                      └───────────────────────┘
```

## 5. Connection Tables

### 5.1 S3 ↔ C3 — Power & UART Link

| Signal | S3 Pin | S3 GPIO | C3 Pin | C3 GPIO |
|---|---|---|---|---|
| Power (5V) | 5V | — | 5V | — |
| Ground | GND | — | GND | — |
| UART TX → RX | D1 | GPIO2 | D7 (RX) | GPIO20 |
| UART RX ← TX | D0 | GPIO1 | D6 (TX) | GPIO21 |

### 5.2 S3 ↔ Button Ladder (Analog Input)

| Ladder Pin | S3 Pin | S3 GPIO | Note |
|---|---|---|---|
| VCC | 3V3 | — | 3.3V logic only — do not use 5V |
| GND | GND | — | |
| Signal | D2 | GPIO3 | S3 strapping pin — see §6 |

### 5.3 C3 ↔ OLED Display (I2C)

| OLED Pin | C3 Pin | C3 GPIO |
|---|---|---|
| VCC | 3V3 | — |
| GND | GND | — |
| SDA | D4 | GPIO6 |
| SCL | D5 | GPIO7 |

### 5.4 C3 ↔ microSD Module (SPI)

Module: Hobby Components HCMODU0074 (level-shifted, onboard 3.3V LDO,
requires 4.5–5.5V VCC).

| SD Module Pin | C3 Pin | C3 GPIO |
|---|---|---|
| VCC | 5V | — |
| GND | GND | — |
| CS | D3 | GPIO5 |
| SCK | D2 | GPIO4 |
| MOSI | D10 | GPIO10 |
| MISO | D1 | GPIO3 |

### 5.5 Full Pin Budget Summary

| Board | Used Pins | Free Pins |
|---|---|---|
| S3 | D0, D1 (UART), D2 (ladder) | **D3 (GPIO4)** |
| C3 | D1, D2, D3, D10 (SD) · D4, D5 (OLED) · D6, D7 (UART) | D0, D8, D9 |

## 6. Button Ladder Calibration

Raw `analogRead()` values captured on **S3 D2 (GPIO3)**, 12-bit ADC
(0–4095 range), with the ladder module powered from **3V3** (not 5V).
~50 samples were logged per state.

| Button state | Observed raw range | Notes |
|---|---|---|
| No button pressed (rest) | 460 – 481 | Baseline / idle divider output |
| Left | 77 – 97 | Two outlier samples (477, 485) discarded — press/release transition noise, not a stable reading |
| Middle | 11 – 22 | |
| Right | 0 (constant) | Fully bottoms out the divider |

**Important — resistor-ladder race condition:** pressing two buttons
simultaneously puts two ladder resistors in parallel, producing a
resistance (and therefore an ADC reading) that does not match *any* single
calibrated button. Open-ended cascading thresholds (e.g. "reading ≤ 300 →
Left") are unsafe, because a stray multi-press value could fall inside a
single button's range and get silently misreported as that button.

Instead, each state is defined as a **tight window** around its observed
cluster, with margin for ADC noise but no overlap between windows. Any
reading that does not fall inside one of the four windows — including
two-button race conditions — is explicitly classified as invalid rather
than defaulted into the nearest bucket.

| State | Observed range | Window used (with margin) |
|---|---|---|
| Right | 0 | 0 – 5 |
| Middle | 11 – 22 | 8 – 26 |
| Left | 77 – 97 | 70 – 105 |
| No button (rest) | 460 – 481 | 450 – 495 |
| *(anything else)* | — | → **Invalid / multi-press** |

**Finalized classifier:**

```cpp
constexpr int LADDER_PIN = 3;   // S3 D2 / GPIO3 / A2

enum LadderButton { BTN_NONE, BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, BTN_INVALID };

bool inRange(int value, int lo, int hi) {
  return value >= lo && value <= hi;
}

LadderButton readLadderButton() {
  int reading = analogRead(LADDER_PIN);

  if (inRange(reading, 0, 5))     return BTN_RIGHT;
  if (inRange(reading, 8, 26))    return BTN_MIDDLE;
  if (inRange(reading, 70, 105))  return BTN_LEFT;
  if (inRange(reading, 450, 495)) return BTN_NONE;

  return BTN_INVALID; // out-of-window: multi-press, noise, or wiring fault
}
```

`BTN_INVALID` should be treated by calling code the same as "no valid
input" — i.e. ignored — rather than acted on.

## 7. Known Hardware Considerations

- **ESP32-C3 strapping pins are GPIO2, GPIO8, GPIO9.** The SD module's SPI
  bus was deliberately remapped away from all three (§5.4) after MISO on
  GPIO9 caused the C3 to lock into UART download mode at boot whenever the
  SD card's DO line pulled the pin low during reset sampling.
- **ESP32-S3 strapping pins are GPIO0, GPIO3, GPIO45, GPIO46.** The button
  ladder's signal line sits on GPIO3 (D2) — functional and verified stable
  across repeated power cycles, but it is the S3's one strapping pin in use
  and should be the first suspect if intermittent boot issues ever appear.
  **D3 (GPIO4)** remains free as a zero-risk fallback pin.
- All analog/ADC-capable pins in the system are now fully committed: S3 has
  one ADC pin left (D3); the C3's remaining free pins (D0, D8, D9) are
  digital-only.

## 8. Bill of Materials & Cost Analysis

Prices below are **typical AliExpress/China-sourced estimates in USD**,
current as of writing. Actual prices vary by seller and shipping tier —
verify current listings before finalizing a competition budget.

| Item | Qty | Est. Unit Price | Est. Subtotal |
|---|---|---|---|
| ReSpeaker Lite kit (dual-mic array + XMOS XU316 + pre-soldered XIAO ESP32-S3) | 1 | $20–25 | $22 |
| Seeed XIAO ESP32-C3 | 1 | $4–6 | $5 |
| microSD SPI module w/ level shifter (HW-125 style) | 1 | $0.80–1.50 | $1 |
| microSD card (8–32 GB, generic) | 1 | $2–5 | $3 |
| SSD1306 0.96" OLED, I2C, 128×64 | 1 | $1.50–3 | $2 |
| 3-button analog resistor-ladder module | 1 | $1–2 | $1.50 |
| Dupont jumper wire set | 1 | $1–2 | $1.50 |
| Misc. (resistor, headers, USB-C cable) | 1 | $1–2 | $1.50 |
| **Total (estimated)** | | | **≈ $37.50** |

**Cost note:** the entire dual-controller system — audio input, SD logging,
OLED display, and analog button input — comes in **under $40** using
commodity China-sourced parts, which is a meaningful cost-efficiency point
if presented alongside functionality at competition.
