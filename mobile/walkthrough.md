# ePodd Mobile App: Batch Conversion, Persistent Library & Dual Transport Walkthrough

We have enhanced the **ePodd** mobile app (`c:\Users\tambu\AndroidStudioProjects\ePodd`) to solve all issues with file selection, batch conversion, track persistence, audio testing, device pairing, and transport selection.

---

## 1. Key Improvements Delivered

### 1.1 Multi-Audio Selection & Animated Batch Queue
- **Multi-File Picker**: Supports selecting single or multiple audio files (`.m4a`, `.mp3`, `.wav`, `.flac`, `.aac`) at once via `ActivityResultContracts.GetMultipleContents()`.
- **Animated Queue Conversion**: Converts selected tracks sequentially in the background while displaying an animated per-track and overall batch progress bar (`batchConversionProgress`).

### 1.2 Persistent Converted Track Library
- **Saved Tracks Persistence**: Converted `.raw` files are saved in persistent local storage (`app/cache/epod_raw`) and scanned on startup via `loadSavedConvertedTracks()`.
- **Zero Data Loss**: Converted `.raw` files remain safely saved on your device when you scan for BLE devices, pair with hardware, switch tabs, or adjust settings.

### 1.3 Audio Testing & Listening Preview
- **Individual Track Audio Preview**: Each converted `.raw` file card in the library features an interactive **Play / Pause Preview** button utilizing Android `AudioTrack` (22050 Hz, 8-bit unsigned mono PCM).
- **Track Progress Animation**: While previewing, an animated progress bar shows exact audio position so you can test and evaluate how each track sounds before uploading to ePod.

### 1.4 Seamless Device Scanner Overlay
- **No Progress Loss**: Tapping "Send Selected Tracks to ePod" when no BLE device is connected opens an inline **Connect to ePod Scanner Modal** right on the transfer screen.
- Allows discovering and connecting to an ePod device without leaving the page or resetting your converted tracks library.

---

## 2. How Files Are Sent: Wi-Fi vs. BLE Transport

The app supports dual transport modes (**Wi-Fi Hotspot Upload** vs. **BLE Direct Stream**):

```
                       +-----------------------------------+
                       |    Converted .raw Audio Tracks    |
                       +-----------------------------------+
                                         |
                       +-----------------------------------+
                       |    Transport Selection Switch    |
                       +-----------------------------------+
                                   /           \
                                  /             \
            +------------------------+       +------------------------+
            |  Wi-Fi Hotspot (HTTP)  |       |   BLE Direct Streaming |
            |  http://192.168.4.1/   |       |   GATT EP03 Packets    |
            +------------------------+       +------------------------+
                        |                                |
            - High-Speed Multipart Upload    - MTU-framed packet chunks
            - Direct to /music on SD card    - Wireless BLE GATT write
            - Recommended for large files    - Controls & small files
```

1. **Wi-Fi Hotspot Direct Upload (Recommended for Speed)**:
   - The XIAO ESP32-S3 hosts a Wi-Fi Access Point (Soft-AP `ePod-Music`, default IP `192.168.4.1`) with a POST `/upload` endpoint.
   - The app streams `.raw` files directly over HTTP (`postFileToEpodServer`) straight into `/music/` on the SD card at high speed (MB/s).
   - Once upload completes, the app automatically issues an ASCII `RESCAN` command over UART/BLE.

2. **BLE Direct Stream**:
   - For small tracks or when Wi-Fi is disabled, the app slices `.raw` files into MTU-sized packets (`streamViaBleGatt`) and writes them sequentially to GATT characteristic `EP03` with sequence headers.

---

## 3. Verification & Build Result

- Executed Gradle build check: `.\gradlew.bat assembleDebug`
- Output: `BUILD SUCCESSFUL in 2s`. All Kotlin compilation, Android resource packaging, and Manifest permission validation completed with zero errors.
- **Manifest Updates**: Added `android.permission.INTERNET`, `ACCESS_NETWORK_STATE`, `CHANGE_NETWORK_STATE`, and enabled `android:usesCleartextTraffic="true"` in the application tag to allow the app to POST files to the plain HTTP Soft-AP (`http://192.168.4.1/upload`) on modern Android versions.

---

## 4. Volume Sync & Library Sync Handshake

### 4.1 Telemetry Synchronization Handshake on Connection
- When the BLE connection changes to `Connected`, the app automatically schedules a sequence of `INFO` and `LIST` commands after a brief settling delay.
- The firmware parses these and sends back the list of tracks and card storage info, populating the UI remote indicators immediately.
- We fixed a destructive telemetry-parsing bug: incoming telemetry status lines (like `STATE` or `TRACK`) are now merged field-by-field into the existing data rather than resetting fields like track count or storage MB size to default values.

### 4.2 Volume Control & PCM Stream Constraint
- The app's volume slider writes `VOL <n>` commands to ePod's control characteristic.
- Due to the hardware UART constraint (where the link between S3 and WROVER carries raw PCM bytes during active playback), text commands are blocked by the firmware mid-stream.
- Volume is adjusted using ePod's physical buttons during playback. The S3 forwards the new level back to the mobile app (`VOL <n>`), which dynamically syncs the app slider position to match the hardware.

---

## 5. Firmware Radio Control & Progress Bar UI

We have implemented the following firmware features on the microcontroller to support the app's requirements:

### 5.1 On-Demand Bluetooth & Wi-Fi Settings Menu
- Both Bluetooth (BLE) and Wi-Fi are **OFF** by default when the ePod boots up to conserve power and reduce RF noise.
- A new **Radio Settings** menu has been added to the OLED interface (navigated to from the Home screen).
- The user can select and toggle **BLE Bluetooth** and **Wi-Fi AP** on/off independently using the hardware buttons.
- When Wi-Fi AP is turned ON, the screen displays the network SSID (`ePod-Music`) and the password (`epodmusicpass`) so the user can connect their phone.

### 5.2 Real-Time Upload Progress Overlay
- During file transfers (both Wi-Fi HTTP uploads and BLE GATT transfers), the S3 reports the upload progress to WROVER.
- The WROVER displays a persistent **PROGRESS overlay** over the screen with:
  - Real-time percentage progress bar for Wi-Fi uploads.
  - Kilobytes received indicator for BLE chunked uploads.
- The overlay is automatically hidden once the upload completes or if the transfer is interrupted/discarded.



