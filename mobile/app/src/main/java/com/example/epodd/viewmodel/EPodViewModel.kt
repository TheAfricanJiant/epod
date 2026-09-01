package com.example.epodd.viewmodel

import android.app.Application
import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.epodd.audio.AudioConverter
import com.example.epodd.audio.AudioPreviewPlayer
import com.example.epodd.audio.ConvertedTrack
import com.example.epodd.audio.DspMode
import com.example.epodd.ble.BleScanner
import com.example.epodd.ble.ConnectionState
import com.example.epodd.ble.DiscoveredDevice
import com.example.epodd.ble.EPodBleManager
import com.example.epodd.ble.PlaybackState
import com.example.epodd.ble.TelemetryData
import com.example.epodd.ble.parseTelemetry
import com.example.epodd.utils.FileShareHandler
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.DataOutputStream
import java.io.File
import java.io.FileInputStream
import java.net.HttpURLConnection
import java.net.URL

enum class TransportMode {
    WIFI_HTTP, // Fast Direct Hotspot Upload (http://192.168.4.1/upload)
    BLE_GATT   // Wireless GATT Packet Streaming (EP03)
}

class EPodViewModel(application: Application) : AndroidViewModel(application) {

    private val TAG = "EPodViewModel"

    val bleManager = EPodBleManager(application.applicationContext)
    val bleScanner = BleScanner(application.applicationContext)
    private val audioConverter = AudioConverter()
    private val audioPreviewPlayer = AudioPreviewPlayer()

    // ── Telemetry & BLE State ──

    private val _telemetryData = MutableStateFlow(TelemetryData())
    val telemetryData: StateFlow<TelemetryData> = _telemetryData.asStateFlow()

    val connectionState: StateFlow<ConnectionState> = bleManager.connectionState
    val mtuSize: StateFlow<Int> = bleManager.mtuSize
    val rssiValue: StateFlow<Int> = bleManager.rssiValue
    val telemetryLogs: StateFlow<List<String>> = bleManager.telemetryLogs
    val isScanning: StateFlow<Boolean> = bleScanner.isScanning
    val discoveredDevices: StateFlow<List<DiscoveredDevice>> = bleScanner.discoveredDevices

    // Volume state (0..100 UI, mapped to 0..24 hardware scale)
    private val _volume = MutableStateFlow(70)
    val volume: StateFlow<Int> = _volume.asStateFlow()

    // ── Converted Tracks Persistent Library ──

    private val _convertedTracks = MutableStateFlow<List<ConvertedTrack>>(emptyList())
    val convertedTracks: StateFlow<List<ConvertedTrack>> = _convertedTracks.asStateFlow()

    // Selection starts empty — user explicitly picks tracks to send
    private val _selectedTrackIds = MutableStateFlow<Set<String>>(emptySet())
    val selectedTrackIds: StateFlow<Set<String>> = _selectedTrackIds.asStateFlow()

    // ── Multi-File Selection & Conversion Queue State ──

    private val _pendingUris = MutableStateFlow<List<Uri>>(emptyList())
    val pendingUris: StateFlow<List<Uri>> = _pendingUris.asStateFlow()

    private val _dspMode = MutableStateFlow(DspMode.VOICE)
    val dspMode: StateFlow<DspMode> = _dspMode.asStateFlow()

    private val _isConverting = MutableStateFlow(false)
    val isConverting: StateFlow<Boolean> = _isConverting.asStateFlow()

    // Overall 0..1 progress across all files in batch
    private val _batchConversionProgress = MutableStateFlow(0f)
    val batchConversionProgress: StateFlow<Float> = _batchConversionProgress.asStateFlow()

    // Name of the file currently being converted (shown live during conversion)
    private val _currentConvertingFileName = MutableStateFlow("")
    val currentConvertingFileName: StateFlow<String> = _currentConvertingFileName.asStateFlow()

    // Index of file currently converting (e.g. "2 / 5")
    private val _currentConvertingIndex = MutableStateFlow(0)
    val currentConvertingIndex: StateFlow<Int> = _currentConvertingIndex.asStateFlow()

    private val _totalConvertingCount = MutableStateFlow(0)
    val totalConvertingCount: StateFlow<Int> = _totalConvertingCount.asStateFlow()

    // ── Audio Preview Player State ──

    val isPreviewPlaying: StateFlow<Boolean> = audioPreviewPlayer.isPlaying
    val activePreviewTrackId: StateFlow<String?> = audioPreviewPlayer.activeTrackId
    val previewProgress: StateFlow<Float> = audioPreviewPlayer.playbackProgress

    // ── Transport & Network State ──

    private val _transportMode = MutableStateFlow(TransportMode.WIFI_HTTP)
    val transportMode: StateFlow<TransportMode> = _transportMode.asStateFlow()

    private val _wifiHostIp = MutableStateFlow("192.168.4.1")
    val wifiHostIp: StateFlow<String> = _wifiHostIp.asStateFlow()

    private val _isDeviceScannerOpen = MutableStateFlow(false)
    val isDeviceScannerOpen: StateFlow<Boolean> = _isDeviceScannerOpen.asStateFlow()

    // ── Transfer Progress ──

    private val _transferProgress = MutableStateFlow(0f)
    val transferProgress: StateFlow<Float> = _transferProgress.asStateFlow()

    private val _transferStatus = MutableStateFlow("Ready")
    val transferStatus: StateFlow<String> = _transferStatus.asStateFlow()

    private val _isTransferring = MutableStateFlow(false)
    val isTransferring: StateFlow<Boolean> = _isTransferring.asStateFlow()

    private var transferJob: Job? = null

    // ── Init ──

    init {
        // Restore saved .raw tracks from disk — selection starts empty
        viewModelScope.launch(Dispatchers.IO) {
            val loaded = audioConverter.loadSavedConvertedTracks(application.applicationContext)
            _convertedTracks.value = loaded
            // Do NOT auto-select — user explicitly selects before sending
        }

        // Observe incoming BLE telemetry
        viewModelScope.launch {
            bleManager.rawTelemetryPayload
                .filterNotNull()
                .collect { bytes -> onTelemetryReceived(bytes) }
        }

        // Auto-query device library and status on connection ready
        viewModelScope.launch {
            connectionState.collect { state ->
                if (state is ConnectionState.Connected) {
                    // Give connection a moment to finalize services discovery and notifications
                    delay(800)
                    bleManager.addLog("Querying ePod device state...")
                    bleManager.writeControlText("INFO")
                    delay(200)
                    bleManager.writeControlText("LIST")
                }
            }
        }
    }

    fun onTelemetryReceived(rawBytes: ByteArray) {
        val textLine = String(rawBytes, Charsets.UTF_8).trim('\u0000', '\r', '\n', ' ')
        if (textLine.isEmpty()) return

        try {
            val current = _telemetryData.value
            var updated = current

            when {
                textLine.startsWith("INFO ") -> {
                    val parts = textLine.split(" ")
                    if (parts.size >= 4) {
                        val tracks = parts[1].toIntOrNull() ?: current.totalTracks
                        val seconds = parts[2].toIntOrNull() ?: current.trackDurationSeconds
                        val cardMB = parts[3].toIntOrNull() ?: current.freeStorageMb
                        updated = current.copy(
                            totalTracks = tracks,
                            trackDurationSeconds = seconds,
                            freeStorageMb = cardMB
                        )
                    }
                }
                textLine.startsWith("TRACK ") -> {
                    val parts = textLine.split(" ", limit = 4)
                    if (parts.size >= 4) {
                        val idx = parts[1].toIntOrNull() ?: current.trackId
                        val total = parts[2].toIntOrNull() ?: current.totalTracks
                        val name = parts[3]
                        updated = current.copy(
                            trackId = idx,
                            totalTracks = total,
                            trackName = name
                        )
                    }
                }
                textLine.startsWith("STATE ") -> {
                    val stateText = textLine.substringAfter("STATE ").trim()
                    updated = current.copy(
                        playbackState = PlaybackState.fromString(stateText)
                    )
                }
                textLine.startsWith("VOL ") -> {
                    val volLevel = textLine.substringAfter("VOL ").trim().toIntOrNull() ?: 16
                    // Sync hardware volume level (0..24) back to app slider (0..100)
                    val volPercent = ((volLevel / 24f) * 100f).toInt().coerceIn(0, 100)
                    _volume.value = volPercent
                }
            }

            _telemetryData.value = updated
            bleManager.addLog("Telemetry parsed: $textLine")
        } catch (e: Exception) {
            bleManager.addLog("Error parsing telemetry '$textLine': ${e.localizedMessage}")
        }
    }

    // ── Multi-File Audio Selection & Batch Queue Conversion ──

    fun onMultipleFilesSelected(uris: List<Uri>) {
        if (uris.isEmpty()) return
        _pendingUris.value = uris
        bleManager.addLog("Selected ${uris.size} file(s) for batch conversion")
    }

    fun clearPendingFiles() {
        _pendingUris.value = emptyList()
    }

    fun setDspMode(mode: DspMode) {
        _dspMode.value = mode
    }

    fun startBatchConversion(context: Context) {
        val uris = _pendingUris.value
        if (uris.isEmpty() || _isConverting.value) return

        val totalFiles = uris.size
        _totalConvertingCount.value = totalFiles

        viewModelScope.launch {
            _isConverting.value = true
            _batchConversionProgress.value = 0f
            audioPreviewPlayer.stop()

            val updatedLibrary = _convertedTracks.value.toMutableList()

            for ((index, uri) in uris.withIndex()) {
                if (!isActive) break

                // ▶ Show the filename BEFORE starting conversion (resolve display name first)
                val displayName = resolveDisplayName(context, uri)
                _currentConvertingFileName.value = displayName
                _currentConvertingIndex.value = index + 1

                val fileBaseProgress = index.toFloat() / totalFiles.toFloat()
                val fileSlice = 1f / totalFiles.toFloat()

                try {
                    val resultTrack = audioConverter.convertAudioFile(
                        context = context,
                        inputUri = uri,
                        mode = _dspMode.value,
                        onProgress = { trackProgress ->
                            _batchConversionProgress.value =
                                (fileBaseProgress + trackProgress * fileSlice).coerceIn(0f, 1f)
                        }
                    )

                    // Remove any previous track with the same output filename (re-convert)
                    updatedLibrary.removeAll { it.sanitizedFileName == resultTrack.sanitizedFileName }
                    updatedLibrary.add(0, resultTrack)
                    _convertedTracks.value = updatedLibrary.toList()

                    bleManager.addLog(
                        "Converted (${index + 1}/$totalFiles): " +
                        "${resultTrack.sanitizedFileName} (${resultTrack.durationSeconds}s)"
                    )
                } catch (e: Exception) {
                    bleManager.addLog("Conversion failed (${index + 1}/$totalFiles) [$displayName]: ${e.localizedMessage}")
                }
            }

            _batchConversionProgress.value = 1f
            _isConverting.value = false
            _pendingUris.value = emptyList()
            _currentConvertingFileName.value = ""
            _currentConvertingIndex.value = 0
            _totalConvertingCount.value = 0

            bleManager.addLog("Batch conversion complete: $totalFiles file(s)")
        }
    }

    /** Resolves a human-readable display name from a URI (used for showing "Converting: X" before codec work starts) */
    private suspend fun resolveDisplayName(context: Context, uri: Uri): String =
        withContext(Dispatchers.IO) {
            try {
                context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                    val idx = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    if (idx != -1 && cursor.moveToFirst()) cursor.getString(idx) else null
                } ?: uri.lastPathSegment ?: "audio"
            } catch (_: Exception) {
                uri.lastPathSegment ?: "audio"
            }
        }

    // ── Converted Track Library Management ──

    fun toggleTrackSelection(trackId: String) {
        val current = _selectedTrackIds.value
        _selectedTrackIds.value = if (current.contains(trackId)) current - trackId else current + trackId
    }

    fun selectAllTracks() {
        _selectedTrackIds.value = _convertedTracks.value.map { it.id }.toSet()
    }

    fun clearTrackSelection() {
        _selectedTrackIds.value = emptySet()
    }

    fun toggleTrackPreview(track: ConvertedTrack) {
        if (activePreviewTrackId.value == track.id && isPreviewPlaying.value) {
            audioPreviewPlayer.stop()
        } else {
            audioPreviewPlayer.playRawFile(track.id, track.file)
        }
    }

    fun stopPreview() {
        audioPreviewPlayer.stop()
    }

    fun deleteTrack(track: ConvertedTrack) {
        audioPreviewPlayer.stop()
        audioConverter.deleteConvertedTrack(track)
        _convertedTracks.value = _convertedTracks.value.filter { it.id != track.id }
        _selectedTrackIds.value = _selectedTrackIds.value - track.id
        bleManager.addLog("Deleted: ${track.sanitizedFileName}")
    }

    fun shareTrack(context: Context, track: ConvertedTrack) {
        FileShareHandler.shareFile(context, track.file)
    }

    // ── Transport & Network Controls ──

    fun setTransportMode(mode: TransportMode) {
        _transportMode.value = mode
        bleManager.addLog("Transport → ${mode.name}")
    }

    fun setWifiHostIp(ip: String) {
        _wifiHostIp.value = ip
    }

    fun openDeviceScanner() {
        _isDeviceScannerOpen.value = true
        startScan()
    }

    fun closeDeviceScanner() {
        _isDeviceScannerOpen.value = false
        stopScan()
    }

    // ── File Upload / Stream Execution ──

    fun sendSelectedTracks(context: Context) {
        val selectedIds = _selectedTrackIds.value
        if (selectedIds.isEmpty()) {
            _transferStatus.value = "Select tracks to send first"
            return
        }
        val tracksToUpload = _convertedTracks.value.filter { selectedIds.contains(it.id) }
        if (tracksToUpload.isEmpty()) {
            _transferStatus.value = "No valid tracks found"
            return
        }

        when (_transportMode.value) {
            TransportMode.WIFI_HTTP -> uploadViaWifiHttp(tracksToUpload)
            TransportMode.BLE_GATT -> {
                if (connectionState.value !is ConnectionState.Connected) {
                    openDeviceScanner()
                    _transferStatus.value = "Connect ePod over BLE first"
                    return
                }
                streamViaBleGatt(tracksToUpload)
            }
        }
    }

    private fun uploadViaWifiHttp(tracksToUpload: List<ConvertedTrack>) {
        if (_isTransferring.value) return

        transferJob = viewModelScope.launch(Dispatchers.IO) {
            _isTransferring.value = true
            _transferProgress.value = 0f
            _transferStatus.value = "Connecting to ePod Wi-Fi…"

            val totalBytes = tracksToUpload.sumOf { it.outputSizeBytes }
            var uploadedBytes = 0L
            val hostIp = _wifiHostIp.value

            bleManager.addLog("Wi-Fi HTTP upload → $hostIp: ${tracksToUpload.size} track(s)")

            for ((index, track) in tracksToUpload.withIndex()) {
                if (!coroutineContext.isActive) break

                _transferStatus.value = "Uploading (${index + 1}/${tracksToUpload.size}): ${track.sanitizedFileName}"
                bleManager.addLog("Uploading: ${track.sanitizedFileName} (${track.outputSizeBytes / 1024} KB)")

                val success = postFileToEpodServer(hostIp, track.file) { bytesSent ->
                    uploadedBytes += bytesSent
                    _transferProgress.value =
                        if (totalBytes > 0) (uploadedBytes.toFloat() / totalBytes.toFloat()).coerceIn(0f, 1f) else 0f
                }

                if (!success) {
                    _transferStatus.value = "Upload failed: ${track.sanitizedFileName} — check Wi-Fi connection"
                    bleManager.addLog("HTTP upload FAILED for ${track.sanitizedFileName}")
                    _isTransferring.value = false
                    return@launch
                }
                bleManager.addLog("Uploaded OK: ${track.sanitizedFileName}")
            }

            if (coroutineContext.isActive) {
                _transferProgress.value = 1f
                _transferStatus.value = "Upload complete: ${tracksToUpload.size} track(s) sent to ePod"
                bleManager.addLog("Wi-Fi upload complete (${tracksToUpload.size} tracks)")
                // Trigger firmware SD card rescan
                bleManager.writeControlText("RESCAN")
            }
            _isTransferring.value = false
        }
    }

    private suspend fun postFileToEpodServer(
        hostIp: String,
        file: File,
        onProgressUpdate: (Long) -> Unit
    ): Boolean = withContext(Dispatchers.IO) {
        val boundary = "====ePodBoundary${System.currentTimeMillis()}===="
        val lineEnd = "\r\n"
        val twoHyphens = "--"

        try {
            val url = URL("http://$hostIp/upload")
            val conn = url.openConnection() as HttpURLConnection
            conn.connectTimeout = 8000
            conn.readTimeout = 30000
            conn.doInput = true
            conn.doOutput = true
            conn.useCaches = false
            conn.requestMethod = "POST"
            conn.setRequestProperty("Connection", "Keep-Alive")
            conn.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")

            DataOutputStream(conn.outputStream).use { dos ->
                dos.writeBytes(twoHyphens + boundary + lineEnd)
                dos.writeBytes(
                    "Content-Disposition: form-data; name=\"file\"; filename=\"${file.name}\"$lineEnd"
                )
                dos.writeBytes("Content-Type: application/octet-stream$lineEnd$lineEnd")

                FileInputStream(file).use { fis ->
                    val buffer = ByteArray(8192)
                    var bytesRead: Int
                    while (fis.read(buffer).also { bytesRead = it } > 0) {
                        dos.write(buffer, 0, bytesRead)
                        onProgressUpdate(bytesRead.toLong())
                    }
                }
                dos.writeBytes(lineEnd)
                dos.writeBytes(twoHyphens + boundary + twoHyphens + lineEnd)
                dos.flush()
            }

            val responseCode = conn.responseCode
            conn.disconnect()
            responseCode in 200..299
        } catch (e: Exception) {
            Log.e(TAG, "Wi-Fi HTTP upload error: ${e.localizedMessage}")
            false
        }
    }

    private fun streamViaBleGatt(tracksToUpload: List<ConvertedTrack>) {
        if (_isTransferring.value) return

        transferJob = viewModelScope.launch(Dispatchers.IO) {
            _isTransferring.value = true
            _transferProgress.value = 0f
            _transferStatus.value = "Starting BLE stream…"

            val currentMtu = bleManager.mtuSize.value
            val payloadChunkSize = (currentMtu - 7).coerceAtLeast(16)
            val totalBytes = tracksToUpload.sumOf { it.outputSizeBytes }
            var transferredTotal = 0L

            bleManager.addLog("BLE GATT stream: ${tracksToUpload.size} track(s), MTU=$currentMtu, chunk=$payloadChunkSize bytes")

            for ((index, track) in tracksToUpload.withIndex()) {
                if (!coroutineContext.isActive) break

                _transferStatus.value = "BLE Stream (${index + 1}/${tracksToUpload.size}): ${track.sanitizedFileName}"
                bleManager.addLog("Streaming: ${track.sanitizedFileName} (${track.outputSizeBytes / 1024} KB)")

                FileInputStream(track.file).use { fis ->
                    val buffer = ByteArray(payloadChunkSize)
                    var bytesRead: Int
                    var seqNum = 0

                    while (coroutineContext.isActive) {
                        bytesRead = fis.read(buffer)
                        if (bytesRead <= 0) break

                        // 4-byte header: [seqHi, seqLo, lenHi, lenLo] + payload
                        val packet = ByteArray(4 + bytesRead)
                        packet[0] = ((seqNum shr 8) and 0xFF).toByte()
                        packet[1] = (seqNum and 0xFF).toByte()
                        packet[2] = ((bytesRead shr 8) and 0xFF).toByte()
                        packet[3] = (bytesRead and 0xFF).toByte()
                        System.arraycopy(buffer, 0, packet, 4, bytesRead)

                        val success = bleManager.writeFileSyncBytes(packet)
                        if (!success) {
                            bleManager.addLog("BLE packet write failed at seq=$seqNum")
                            break
                        }

                        seqNum++
                        transferredTotal += bytesRead
                        _transferProgress.value =
                            (transferredTotal.toFloat() / totalBytes.toFloat()).coerceIn(0f, 1f)
                        delay(12L) // ~83 packets/sec throttle
                    }
                }

                bleManager.addLog("Streamed OK: ${track.sanitizedFileName}")
            }

            if (coroutineContext.isActive && transferredTotal >= totalBytes) {
                _transferProgress.value = 1f
                _transferStatus.value = "BLE stream complete: ${tracksToUpload.size} track(s)"
                bleManager.addLog("BLE GATT stream complete (${tracksToUpload.size} tracks)")
                bleManager.writeControlText("RESCAN")
            }
            _isTransferring.value = false
        }
    }

    fun cancelFileTransfer() {
        transferJob?.cancel()
        _isTransferring.value = false
        _transferStatus.value = "Transfer cancelled"
        bleManager.addLog("Transfer cancelled by user")
    }

    // ── Remote Control Commands ──

    fun sendPlay() {
        bleManager.writeControlText("PLAY")
        _telemetryData.value = _telemetryData.value.copy(playbackState = PlaybackState.PLAYING)
    }

    fun sendPause() {
        bleManager.writeControlText("PAUSE")
        _telemetryData.value = _telemetryData.value.copy(playbackState = PlaybackState.PAUSED)
    }

    fun sendStop() {
        bleManager.writeControlText("STOP")
        _telemetryData.value = _telemetryData.value.copy(playbackState = PlaybackState.STOPPED)
    }

    fun sendNext() = bleManager.writeControlText("NEXT")

    fun sendPrev() = bleManager.writeControlText("PREV")

    fun sendVolume(volumeLevel: Int) {
        val clamped = volumeLevel.coerceIn(0, 100)
        _volume.value = clamped
        val hwVolume = ((clamped / 100f) * 24f).toInt().coerceIn(0, 24)
        bleManager.writeControlText("VOL $hwVolume")
    }

    fun sendRescan() = bleManager.writeControlText("RESCAN")

    // ── Scanner Controls ──

    fun startScan() = bleScanner.startScan()

    fun stopScan() = bleScanner.stopScan()

    fun connectToDevice(device: android.bluetooth.BluetoothDevice) {
        bleScanner.stopScan()
        closeDeviceScanner()
        bleManager.connectToDevice(device)
    }

    fun disconnect() = bleManager.disconnectDevice()

    override fun onCleared() {
        super.onCleared()
        audioPreviewPlayer.stop()
        bleScanner.stopScan()
        bleManager.disconnectDevice()
    }
}
