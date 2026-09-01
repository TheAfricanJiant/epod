package com.example.epodd.ble

enum class PlaybackState(val label: String) {
    STOPPED("STOPPED"),
    PLAYING("PLAYING"),
    PAUSED("PAUSED"),
    CLOSING("CLOSING");

    companion object {
        fun fromString(stateStr: String): PlaybackState = when (stateStr.trim().uppercase()) {
            "PLAYING" -> PLAYING
            "PAUSED" -> PAUSED
            "CLOSING" -> CLOSING
            else -> STOPPED
        }
    }
}

data class TelemetryData(
    val trackId: Int = 0,
    val totalTracks: Int = 0,
    val playbackState: PlaybackState = PlaybackState.STOPPED,
    val batteryLevel: Int = 100,
    val freeStorageMb: Int = 0,
    val trackDurationSeconds: Int = 0,
    val trackName: String = "No Track Loaded"
)

/**
 * Parses ePod ASCII telemetry lines sent by S3/WROVER over UART/BLE/Wi-Fi:
 * - "INFO <tracks> <seconds> <cardMB>"
 * - "TRACK <index> <total> <name>"
 * - "STATE <PLAYING|STOPPED|PAUSED>"
 * - "VOL <level>"
 */
fun ByteArray.parseTelemetry(): TelemetryData? {
    if (isEmpty()) return null
    val textLine = String(this, Charsets.UTF_8).trim('\u0000', '\r', '\n', ' ')
    if (textLine.isEmpty()) return null

    return try {
        when {
            textLine.startsWith("INFO ") -> {
                val parts = textLine.split(" ")
                if (parts.size >= 4) {
                    val tracks = parts[1].toIntOrNull() ?: 0
                    val seconds = parts[2].toIntOrNull() ?: 0
                    val cardMB = parts[3].toIntOrNull() ?: 0
                    TelemetryData(
                        totalTracks = tracks,
                        trackDurationSeconds = seconds,
                        freeStorageMb = cardMB
                    )
                } else null
            }
            textLine.startsWith("TRACK ") -> {
                val parts = textLine.split(" ", limit = 4)
                if (parts.size >= 4) {
                    val idx = parts[1].toIntOrNull() ?: 1
                    val total = parts[2].toIntOrNull() ?: 1
                    val name = parts[3]
                    TelemetryData(
                        trackId = idx,
                        totalTracks = total,
                        trackName = name
                    )
                } else null
            }
            textLine.startsWith("STATE ") -> {
                val stateText = textLine.substringAfter("STATE ").trim()
                TelemetryData(
                    playbackState = PlaybackState.fromString(stateText)
                )
            }
            else -> null
        }
    } catch (e: Exception) {
        null
    }
}
