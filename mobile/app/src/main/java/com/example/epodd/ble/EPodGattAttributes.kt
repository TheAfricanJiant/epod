package com.example.epodd.ble

import java.util.UUID

object EPodGattAttributes {
    // e-Pod Base Service UUID & Characteristics
    // Raw UUID String: 0000EPOD-0000-1000-8000-00805F9B34FB (Hex representation 00004550...)
    val SERVICE_UUID: UUID = parseUuidSafely("0000EPOD-0000-1000-8000-00805F9B34FB", "00004550-0000-1000-8000-00805F9B34FB")

    // EP01: Control Characteristic (Play/Pause/Next/Prev/Vol)
    val CONTROL_CHAR_UUID: UUID = parseUuidSafely("0000EP01-0000-1000-8000-00805F9B34FB", "00004501-0000-1000-8000-00805F9B34FB")

    // EP02: Telemetry Characteristic (Track info, battery %, status notifications)
    val TELEMETRY_CHAR_UUID: UUID = parseUuidSafely("0000EP02-0000-1000-8000-00805F9B34FB", "00004502-0000-1000-8000-00805F9B34FB")

    // EP03: File Sync Characteristic (Chunked audio streaming to MicroSD)
    val FILE_SYNC_CHAR_UUID: UUID = parseUuidSafely("0000EP03-0000-1000-8000-00805F9B34FB", "00004503-0000-1000-8000-00805F9B34FB")

    private fun parseUuidSafely(rawStr: String, fallbackHexStr: String): UUID {
        return try {
            val hexClean = rawStr.uppercase()
                .replace("EPOD", "4550")
                .replace("EP01", "4501")
                .replace("EP02", "4502")
                .replace("EP03", "4503")
            UUID.fromString(hexClean)
        } catch (e: Exception) {
            UUID.fromString(fallbackHexStr)
        }
    }
}
