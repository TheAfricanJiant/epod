package com.example.epodd.ble

import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import no.nordicsemi.android.ble.BleManager
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

sealed class ConnectionState {
    object Disconnected : ConnectionState()
    object Connecting : ConnectionState()
    object Connected : ConnectionState()
    object Disconnecting : ConnectionState()
    data class Failed(val reason: String) : ConnectionState()
}

class EPodBleManager(context: Context) : BleManager(context) {

    private val TAG = "EPodBleManager"

    // GATT Characteristics
    private var controlCharacteristic: BluetoothGattCharacteristic? = null
    private var telemetryCharacteristic: BluetoothGattCharacteristic? = null
    private var fileSyncCharacteristic: BluetoothGattCharacteristic? = null

    // State Flows
    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _mtuSize = MutableStateFlow(23)
    val mtuSize: StateFlow<Int> = _mtuSize.asStateFlow()

    private val _rssiValue = MutableStateFlow(-1)
    val rssiValue: StateFlow<Int> = _rssiValue.asStateFlow()

    private val _rawTelemetryPayload = MutableStateFlow<ByteArray?>(null)
    val rawTelemetryPayload: StateFlow<ByteArray?> = _rawTelemetryPayload.asStateFlow()

    private val _telemetryLogs = MutableStateFlow<List<String>>(
        listOf(
            "[System] BLE Infrastructure Initialized",
            "[System] Service UUID: ${EPodGattAttributes.SERVICE_UUID}"
        )
    )
    val telemetryLogs: StateFlow<List<String>> = _telemetryLogs.asStateFlow()

    private val _lastTelemetryData = MutableStateFlow("")
    val lastTelemetryData: StateFlow<String> = _lastTelemetryData.asStateFlow()

    init {
        setConnectionObserver(object : no.nordicsemi.android.ble.observer.ConnectionObserver {
            override fun onDeviceConnecting(device: BluetoothDevice) {
                _connectionState.value = ConnectionState.Connecting
                addLog("Connecting to ${device.name ?: device.address}...")
            }

            override fun onDeviceConnected(device: BluetoothDevice) {
                addLog("Connected -> ${device.name ?: device.address}")
            }

            override fun onDeviceFailedToConnect(device: BluetoothDevice, reason: Int) {
                _connectionState.value = ConnectionState.Failed("Reason code: $reason")
                addLog("Failed to connect to ${device.address} (Code: $reason)")
            }

            override fun onDeviceReady(device: BluetoothDevice) {
                _connectionState.value = ConnectionState.Connected
                addLog("GATT Device Ready & Configured")
            }

            override fun onDeviceDisconnecting(device: BluetoothDevice) {
                _connectionState.value = ConnectionState.Disconnecting
                addLog("Disconnecting...")
            }

            override fun onDeviceDisconnected(device: BluetoothDevice, reason: Int) {
                _connectionState.value = ConnectionState.Disconnected
                addLog("Disconnected (Reason: $reason)")
            }
        })
    }

    override fun log(priority: Int, message: String) {
        Log.println(priority, TAG, message)
    }

    override fun getGattCallback(): BleManagerGattCallback {
        return EPodBleManagerGattCallback()
    }

    private inner class EPodBleManagerGattCallback : BleManagerGattCallback() {

        override fun isRequiredServiceSupported(gatt: BluetoothGatt): Boolean {
            val service = gatt.getService(EPodGattAttributes.SERVICE_UUID)
            if (service != null) {
                controlCharacteristic = service.getCharacteristic(EPodGattAttributes.CONTROL_CHAR_UUID)
                telemetryCharacteristic = service.getCharacteristic(EPodGattAttributes.TELEMETRY_CHAR_UUID)
                fileSyncCharacteristic = service.getCharacteristic(EPodGattAttributes.FILE_SYNC_CHAR_UUID)

                val hasControl = controlCharacteristic != null
                val hasTelemetry = telemetryCharacteristic != null
                val hasFileSync = fileSyncCharacteristic != null

                addLog("GATT Service Discovery: Control=$hasControl, Telemetry=$hasTelemetry, FileSync=$hasFileSync")
                return hasControl && hasTelemetry && hasFileSync
            }
            addLog("e-Pod Service UUID not found on device")
            return false
        }

        override fun initialize() {
            // Crucial: Automatically request expanded MTU size (517 bytes) as required by Prompt 2 spec
            requestMtu(517)
                .with { _, mtu ->
                    _mtuSize.value = mtu
                    addLog("Negotiated MTU size: $mtu bytes")
                }
                .enqueue()

            // Enable Notifications on Telemetry Characteristic (EP02)
            telemetryCharacteristic?.let { char ->
                setNotificationCallback(char).with { _, data ->
                    val bytes = data.value
                    if (bytes != null) {
                        _rawTelemetryPayload.value = bytes
                        val hexStr = bytes.joinToString(" ") { String.format("%02X", it) }
                        addLog("Telemetry Rx (${bytes.size}B): $hexStr")
                    }
                }
                enableNotifications(char).enqueue()
                addLog("Notifications enabled for Telemetry (EP02)")
            }

            // Read initial RSSI
            readRssi()
                .with { _, rssi ->
                    _rssiValue.value = rssi
                    addLog("RSSI: $rssi dBm")
                }
                .enqueue()
        }

        override fun onServicesInvalidated() {
            controlCharacteristic = null
            telemetryCharacteristic = null
            fileSyncCharacteristic = null
        }
    }

    fun connectToDevice(device: BluetoothDevice) {
        addLog("Initiating BLE connection to ${device.address} with retry(3, 100)")
        connect(device)
            .retry(3, 100)
            .useAutoConnect(false)
            .enqueue()
    }

    fun disconnectDevice() {
        disconnect().enqueue()
    }

    fun writeControlText(commandText: String) {
        val char = controlCharacteristic
        val payload = if (commandText.endsWith("\n")) commandText else "$commandText\n"
        val bytes = payload.toByteArray(Charsets.UTF_8)
        if (char != null) {
            writeCharacteristic(char, bytes, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
                .with { _, _ ->
                    addLog("ASCII Tx: ${commandText.trim()}")
                }
                .enqueue()
        } else {
            addLog("ASCII Cmd (Offline): ${commandText.trim()}")
        }
    }

    fun writeControlBytes(bytes: ByteArray) {
        val char = controlCharacteristic ?: return
        val hexCmd = bytes.joinToString(" ") { String.format("0x%02X", it) }
        writeCharacteristic(char, bytes, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
            .with { _, _ ->
                addLog("Control Tx (NoResponse): $hexCmd")
            }
            .enqueue()
    }

    suspend fun writeFileSyncBytes(bytes: ByteArray): Boolean = kotlinx.coroutines.suspendCancellableCoroutine { continuation ->
        val char = fileSyncCharacteristic
        if (char == null) {
            if (continuation.isActive) continuation.resumeWith(Result.success(false))
            return@suspendCancellableCoroutine
        }
        writeCharacteristic(char, bytes, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
            .done {
                if (continuation.isActive) continuation.resumeWith(Result.success(true))
            }
            .fail { _, status ->
                addLog("FileSync Tx failed (Status: $status)")
                if (continuation.isActive) continuation.resumeWith(Result.success(false))
            }
            .enqueue()
    }

    fun addLog(message: String) {
        val timeStamp = SimpleDateFormat("HH:mm:ss.SSS", Locale.US).format(Date())
        val entry = "[$timeStamp] $message"
        val updated = _telemetryLogs.value.toMutableList().apply {
            add(entry)
            if (size > 100) removeAt(0) // Keep last 100 log entries
        }
        _telemetryLogs.value = updated
    }
}
