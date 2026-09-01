# ePodd walkthrough: converting, storing and sending tracks

Notes on how file selection, batch conversion, the saved library, playback
testing, pairing and transport selection work in the ePodd app.

---

## 1. Converting and storing tracks

### 1.1 Picking several files at once

The picker takes one file or many (`.m4a`, `.mp3`, `.wav`, `.flac`, `.aac`)
through `ActivityResultContracts.GetMultipleContents()`. Selected tracks are
converted one after another in the background, and the app shows progress for
the current file and for the batch (`batchConversionProgress`).

### 1.2 The library stays put

Converted `.raw` files are written to `app/cache/epod_raw` and read back at
startup by `loadSavedConvertedTracks()`. They stay there when you scan for BLE
devices, pair with the hardware, switch tabs, or change settings.

### 1.3 Listening before you send

Every converted file in the library has a play/pause button that uses Android
`AudioTrack` at 22050 Hz, 8-bit unsigned mono, so you hear exactly what the ePod
will play. A progress bar shows the position while it plays.

### 1.4 Connecting without losing your place

If you tap "Send Selected Tracks to ePod" with no device connected, a scanner
opens over the transfer screen instead of navigating away. You can find and
connect to an ePod without clearing the converted library or your selection.

---

## 2. Wi-Fi and BLE transfer

```
                       +-----------------------------------+
                       |    Converted .raw tracks          |
                       +-----------------------------------+
                                         |
                       +-----------------------------------+
                       |    Pick a transport               |
                       +-----------------------------------+
                                   /           \
                                  /             \
            +------------------------+    +------------------------+
            |  Wi-Fi over HTTP       |    |  BLE transfer          |
            |  http://192.168.4.1/   |    |  GATT EP03 packets     |
            +------------------------+    +------------------------+
                        |                             |
            - multipart upload             - MTU-sized chunks
            - straight to /music on SD     - GATT writes
            - better for large files       - control and small files
```

1. **Wi-Fi upload (faster).** The XIAO ESP32-S3 hosts an access point
   (`ePod-Music`, default IP `192.168.4.1`) with a `POST /upload` endpoint. The
   app streams `.raw` files over HTTP (`postFileToEpodServer`) into `/music/` on
   the SD card at megabytes per second. When the upload finishes it sends
   `RESCAN` so the device re-indexes the card.

2. **BLE transfer.** For small tracks, or when Wi-Fi is off, the app splits the
   file into MTU-sized packets (`streamViaBleGatt`) and writes them in order to
   GATT characteristic `EP03`, each with a sequence header.

---

## 3. Build check

`.\gradlew.bat assembleDebug` completes with `BUILD SUCCESSFUL`.

The manifest needs `android.permission.INTERNET`, `ACCESS_NETWORK_STATE` and
`CHANGE_NETWORK_STATE`, plus `android:usesCleartextTraffic="true"` on the
application tag. Without the last one, recent Android versions block the plain
HTTP POST to `http://192.168.4.1/upload`.

---

## 4. Volume and library sync

### 4.1 What happens when the app connects

When the BLE connection becomes `Connected`, the app waits briefly for the link
to settle and then sends `INFO` and `LIST`. The firmware replies with the track
list and card storage, which fills in the remote screen right away.

Telemetry lines such as `STATE` or `TRACK` are merged field by field into the
existing data. An earlier version replaced the whole record, which reset the
track count and storage size to defaults every time a partial line arrived.

### 4.2 Volume during playback

The volume slider sends `VOL <n>` to the ePod's control characteristic. While a
track is playing, the UART link between the S3 and the WROOM is carrying raw PCM
bytes, so the firmware blocks text commands on it.

During playback, use the buttons on the ePod itself. The S3 sends the new level
back to the app as `VOL <n>`, and the slider moves to match.

---

## 5. Radios and the progress overlay

### 5.1 Turning the radios on when you need them

BLE and Wi-Fi are both **off** when the ePod boots, to save power and keep RF
noise down. A **Radio Settings** menu on the OLED, reached from the Home screen,
toggles each one independently with the hardware buttons. With the Wi-Fi AP on,
the screen shows the network name (`ePod-Music`) and password (`epodmusicpass`)
so you can connect a phone.

### 5.2 Upload progress on the OLED

During a transfer, over Wi-Fi or BLE, the S3 reports progress to the WROOM,
which draws an overlay on top of whatever screen is showing:

- a percentage bar for Wi-Fi uploads
- kilobytes received for BLE transfers

The overlay disappears when the upload finishes or the transfer is interrupted.
