<div align="center">

# 🎵 ePodd — ePod Companion App

**A high-performance Android companion application, audio DSP encoder, and wireless transport manager for the ePod Hardware Music Player.**

[![Android](https://img.shields.io/badge/Platform-Android_8.0+-3DDC84?style=for-the-badge&logo=android&logoColor=white)](https://android.com)
[![Kotlin](https://img.shields.io/badge/Language-Kotlin-7F52FF?style=for-the-badge&logo=kotlin&logoColor=white)](https://kotlinlang.org)
[![Jetpack Compose](https://img.shields.io/badge/UI-Jetpack_Compose-4285F4?style=for-the-badge&logo=jetpackcompose&logoColor=white)](https://developer.android.com/jetpack/compose)
[![Bluetooth LE](https://img.shields.io/badge/Wireless-BLE_GATT-0082FC?style=for-the-badge&logo=bluetooth&logoColor=white)](#)
[![Wi-Fi HTTP](https://img.shields.io/badge/Wireless-Wi--Fi_Soft--AP-FF9900?style=for-the-badge&logo=wifi&logoColor=white)](#)

<br />

> **ePodd** bridges mobile audio libraries with low-power ePod microcontrollers. It decodes audio tracks into ePod-compatible 8-bit unsigned PCM streams, offers live preview listening on device speakers, controls ePod playback via ASCII hardware protocols, and transfers tracks over high-speed Wi-Fi or direct Bluetooth Low Energy (BLE).

</div>

---

## 📱 Interface Showcase

<div align="center">
  <h3>✨ Mobile App Experience</h3>
  <p><i>Sleek, dark-mode native interface powered by Jetpack Compose</i></p>
  
  <br/>

  <table>
    <tr>
      <td width="33%" align="center">
        <b>📡 BLE Device Discovery</b><br/><br/>
        <img src="./mobile/Screenshot_2026-08-30-13-54-44-36_dc92c9c3c7862d337cc55f1d243ecead.jpg" alt="BLE Scanner" width="280"/>
      </td>
      <td width="33%" align="center">
        <b>🎵 Remote Control & Visualizer</b><br/><br/>
        <img src="./mobile/Screenshot_2026-08-30-13-56-07-30_dc92c9c3c7862d337cc55f1d243ecead.jpg" alt="Remote Control" width="280"/>
      </td>
      <td width="33%" align="center">
        <b>📊 Hardware Telemetry & Logs</b><br/><br/>
        <img src="./mobile/Screenshot_2026-08-30-13-57-13-84_dc92c9c3c7862d337cc55f1d243ecead.jpg" alt="Status Telemetry" width="280"/>
      </td>
    </tr>
    <tr>
      <td align="center"><i>Scan & connect to ePod via BLE GATT with live RSSI signal indicators</i></td>
      <td align="center"><i>Real-time audio visualizer bar, transport controls, and physical volume sync</i></td>
      <td align="center"><i>Live battery gauge, 128GB SD storage meter, and active ASCII console log</i></td>
    </tr>
  </table>

  <br/><br/>

  <table>
    <tr>
      <td width="33%" align="center">
        <b>🎛️ Audio Encoder & Queue</b><br/><br/>
        <img src="./mobile/Screenshot_2026-08-30-13-56-33-09_dc92c9c3c7862d337cc55f1d243ecead.jpg" alt="Audio Encoder" width="280"/>
      </td>
      <td width="33%" align="center">
        <b>⚡ Live DSP Batch Encoding</b><br/><br/>
        <img src="./mobile/Screenshot_2026-08-30-13-56-40-45_dc92c9c3c7862d337cc55f1d243ecead.jpg" alt="Batch Encoding" width="280"/>
      </td>
      <td width="33%" align="center">
        <b>🎧 Converted Track Preview</b><br/><br/>
        <img src="./mobile/Screenshot_2026-08-30-13-56-55-05_dc92c9c3c7862d337cc55f1d243ecead.jpg" alt="Library Preview" width="280"/>
      </td>
    </tr>
    <tr>
      <td align="center"><i>Pick audio files with Voice (De-ess) or Loud (Treble) DSP sound profiles</i></td>
      <td align="center"><i>Animated background conversion queue with real-time percentage indicators</i></td>
      <td align="center"><i>Listen to 8-bit mono PCM playback on mobile speakers before transferring</i></td>
    </tr>
  </table>

  <br/><br/>

  <table>
    <tr>
      <td width="50%" align="center">
        <b>📡 Connected BLE Link (MTU 256)</b><br/><br/>
        <img src="./mobile/Screenshot_2026-08-30-13-55-47-03_dc92c9c3c7862d337cc55f1d243ecead.jpg" alt="Connected BLE" width="280"/>
      </td>
      <td width="50%" align="center">
        <b>🚀 Direct BLE Stream Transfer</b><br/><br/>
        <img src="./mobile/Screenshot_2026-08-30-13-57-02-60_dc92c9c3c7862d337cc55f1d243ecead.jpg" alt="BLE Stream Transfer" width="280"/>
      </td>
    </tr>
    <tr>
      <td align="center"><i>Maintains persistent connection with automatic telemetry polling</i></td>
      <td align="center"><i>Chunked MTU wireless streaming with live upload progress tracking</i></td>
    </tr>
  </table>

</div>

---

## 🖼️ Feature Showcase Collages

<div align="center">

| 🎵 Remote Control & BLE Pairing | 📊 System Telemetry & Library Transfer |
| :---: | :---: |
| <img src="./mobile/collage/cwf7kDLINE.png" alt="Remote Control Collage" width="440"/> | <img src="./mobile/collage/0WO6YfaFjY.png" alt="Telemetry Collage" width="440"/> |

</div>

---

## ✨ Key Features

### 🎛️ 1. Professional DSP Audio Converter
- **Multi-Format Decoding**: Decodes `.mp3`, `.m4a`, `.wav`, `.flac`, and `.aac` audio files into ePod headerless `.raw` PCM format.
- **22050 Hz 8-bit Quantizer**: Converts multi-channel audio to single-channel mono ($\frac{L+R}{2}$) sampled at 22,050 Hz with 8-bit unsigned quantization (`0x01` to `0xFF`).
- **UART Byte Safety (Clamping)**: Automatically replaces `0x00` null bytes with `0x01` to prevent premature UART stream termination on ePod microcontrollers.
- **DSP Preset Filter Modes**:
  - 🗣️ **Voice Mode (De-ess)**: Attenuates sub-bass (80Hz high-pass filter), applies notch filtering at 6.5kHz (-7dB), normalizes dynamic range, and tames harsh consonants.
  - 🎸 **Loud Mode (Treble)**: Retains full high-frequency brilliance for instrumental music and acoustic tracks.
- **Batch Processing Queue**: Converts multi-file batches sequentially with per-file progress animations.

### 🎧 2. Persistent Library & Mobile Audio Preview
- **Local Cache Storage**: Converted tracks are persisted in app cache (`/epod_raw`), surviving app restarts, BLE pair events, and tab switches.
- **In-App PCM Preview**: Listen to processed 22050 Hz 8-bit PCM tracks directly on your mobile phone's speaker via native Android `AudioTrack` before sending them to ePod hardware.
- **Library Management**: Selective track checkbox queuing, sharing, and cache cleanup.

### 📡 3. Dual Wireless Transport Engine
- **🚀 Wi-Fi Soft-AP Direct Upload (High Speed)**:
  - Connects to ePod's Wi-Fi Access Point (`ePod-Music` / `http://192.168.4.1/upload`).
  - Streams multi-part HTTP file packets directly onto ePod's SD card at high speed.
  - Automatically triggers an ASCII `RESCAN` command upon upload completion.
- **📶 BLE Direct GATT Streaming**:
  - Frames tracks into MTU-chunked GATT packets (Characteristic `EP03`) for seamless streaming without needing Wi-Fi.

### 🎛️ 4. Hardware ASCII Remote Control
- **Live Visualizer**: Interactive dynamic equalizer visualizer.
- **Transport Controls**: Hardware `PLAY`, `PAUSE`, `STOP`, `NEXT`, `PREV` trigger commands.
- **Bidirectional Volume Sync**: Adjusts ePod volume (`VOL <n>`) and syncs slider when volume is adjusted via physical ePod hardware buttons.
- **Overlay Device Scanner**: Connect to new hardware without losing active conversion queues or track selections.

### 📊 5. Real-Time Telemetry & System Diagnostics
- **Battery & Storage Monitors**: Circular gauge indicators showing battery percentage and free SD capacity (GB).
- **System Console Log**: Live terminal window displaying raw ASCII Rx/Tx telemetry packet exchanges and status notifications.

---

## 🏗️ Architecture & Data Flow

```
                           +-------------------------------------+
                           |      Audio File Selection           |
                           |   (MP3, M4A, WAV, FLAC, AAC)        |
                           +-------------------------------------+
                                              |
                                              v
                           +-------------------------------------+
                           |     Android DSP Audio Converter     |
                           |  - Resample: 22050 Hz Mono          |
                           |  - Quantize: 8-bit Unsigned (0x01-FF)|
                           |  - Filter: Voice De-ess / Loud Mode |
                           |  - Clamp: 0x00 -> 0x01 UART Safety  |
                           +-------------------------------------+
                                              |
                                              v
                           +-------------------------------------+
                           |   Persistent Converted Library      |
                           |  - Local Storage Cache              |
                           |  - In-App AudioTrack Preview        |
                           +-------------------------------------+
                                              |
                                              v
                           +-------------------------------------+
                           |     Transport Selection Switch      |
                           +-------------------------------------+
                                       /             \
                                      /               \
        +----------------------------------+     +----------------------------------+
        |   Wi-Fi Soft-AP Upload (HTTP)    |     |    BLE Direct GATT Streaming     |
        |   http://192.168.4.1/upload      |     |    GATT EP03 Packet Framing      |
        +----------------------------------+     +----------------------------------+
                        |                                          |
                        +--------------------+---------------------+
                                             |
                                             v
                           +-------------------------------------+
                           |        ePod Hardware Device         |
                           |   - Microcontroller DAC DMA         |
                           |   - SD Card Storage (/music/)       |
                           +-------------------------------------+
```

---

## 🛠️ Technical Specifications

### Audio Format Specification (ePod DAC Standard)

| Attribute | Value / Format | Engineering Rationale |
| :--- | :--- | :--- |
| **Codec / Stream** | Raw PCM (Headerless) | Direct zero-overhead hardware DAC DMA buffer streaming |
| **Quantization** | Unsigned 8-bit (`0x01` - `0xFF`) | Matches microcontroller DAC hardware resolution |
| **Sample Rate** | 22,050 Hz | Fixed DAC hardware clock frequency |
| **Channels** | 1 (Mono) | Averages stereo channels: $(L + R) / 2$ |
| **Silence Bias** | `0x80` (128) | Unsigned mid-level zero crossing |
| **Byte Clamping** | `0x00` $\rightarrow$ `0x01` | Avoids premature UART string termination (`\0`) |
| **File Limit** | 32 character alphanumeric | Clean FAT32 SD card filesystem compatibility |

---

## 📡 Hardware ASCII Protocol Command Suite

The mobile app communicates with ePod hardware via standard ASCII string packets over BLE / Serial UART:

| Command | Action / Payload | Description |
| :--- | :--- | :--- |
| `PLAY` | Transport Control | Resumes hardware audio playback |
| `PAUSE` | Transport Control | Pauses hardware audio playback |
| `STOP` | Transport Control | Stops audio playback and resets DAC buffer |
| `NEXT` | Track Skip | Advances to the next track on SD card |
| `PREV` | Track Skip | Skips to the previous track on SD card |
| `VOL <0-100>` | Volume Sync | Sets hardware DAC output volume level |
| `INFO` | Telemetry Query | Requests battery level and SD storage status |
| `LIST` | Library Query | Requests current track index and total tracks |
| `RESCAN` | File Rescan | Triggers SD card filesystem re-index |

---

## ⚙️ Compilation & Build Instructions

### Prerequisites
- **Android Studio** (Ladybug / Iguana or newer)
- **JDK 17** or higher
- **Android SDK API level 34**

### Build Debug APK
Execute the Gradle wrapper command in terminal:

```bash
# Windows
.\gradlew.bat assembleDebug

# Linux / macOS
./gradlew assembleDebug
```

The output APK will be generated at:
`app/build/outputs/apk/debug/app-debug.apk`

---

## 📱 Android Network & Permission Manifest

The app is pre-configured with the required permissions for local HTTP uploads and BLE scanning:

```xml
<!-- Bluetooth & Location Permissions -->
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />

<!-- Network & Cleartext HTTP Permissions for ePod Soft-AP -->
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
<uses-permission android:name="android.permission.CHANGE_NETWORK_STATE" />

<application
    android:usesCleartextTraffic="true" ... >
```

---

<div align="center">

### 💡 Developed for the ePod Music System

*Crafted with Jetpack Compose & Modern Android Development Practices*

</div>
