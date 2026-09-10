package com.example.displayconnect.network

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.ParcelUuid
import android.os.SystemClock
import android.util.Log
import com.example.displayconnect.models.ConnectionState
import com.example.displayconnect.protocol.NavMessage
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean

/**
 * BLE UART (Nordic NUS) client for JSON navigation updates to the ESP32 CYD.
 * Framing: each JSON message ends with '\n'; large payloads are chunked to fit MTU.
 */
@SuppressLint("MissingPermission")
class BleNavClient(context: Context) {

    private val appContext = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val bluetoothManager =
        appContext.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val adapter: BluetoothAdapter? get() = bluetoothManager.adapter

    @Volatile private var gatt: BluetoothGatt? = null
    @Volatile private var rxCharacteristic: BluetoothGattCharacteristic? = null
    @Volatile private var writeChunkSize = DEFAULT_CHUNK
    private var reconnectJob: Job? = null
    private var heartbeatJob: Job? = null
    private var scanJob: Job? = null
    private var connectionTimeoutJob: Job? = null
    private var waitingForMtu = false
    private val shouldReconnect = AtomicBoolean(false)
    private data class OutgoingFrame(val connection: BluetoothGatt,
        val characteristic: BluetoothGattCharacteristic, val bytes: ByteArray)
    private val outgoing = Channel<OutgoingFrame>(Channel.CONFLATED)
    @Volatile private var lastQueuedAt = 0L

    init {
        scope.launch {
            for (frame in outgoing) {
                try {
                    writeInChunks(frame.connection, frame.characteristic, frame.bytes)
                } catch (error: SecurityException) {
                    failConnection(frame.connection, "Bluetooth permission lost while sending")
                }
            }
        }
    }

    private var targetAddress: String = ""

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _scannedDevices = MutableStateFlow<List<BleDeviceItem>>(emptyList())
    val scannedDevices: StateFlow<List<BleDeviceItem>> = _scannedDevices.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    private val deviceMap = ConcurrentHashMap<String, BleDeviceItem>()

    fun isBluetoothEnabled(): Boolean = adapter?.isEnabled == true

    fun startScan(durationMs: Long = SCAN_DURATION_MS) {
        val scanner = adapter?.bluetoothLeScanner
        if (scanner == null || !isBluetoothEnabled()) {
            _isScanning.value = false
            return
        }

        stopScanInternal()
        deviceMap.clear()
        _scannedDevices.value = emptyList()
        _isScanning.value = true

        val filters = listOf(
            ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE_UUID)).build()
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        try {
            scanner.startScan(filters, settings, scanCallback)
        } catch (_: SecurityException) {
            try {
                scanner.startScan(null, settings, scanCallback)
            } catch (_: SecurityException) {
                _isScanning.value = false
                return
            }
        }

        scanJob = scope.launch {
            delay(durationMs)
            stopScanInternal()
        }
    }

    fun stopScan() {
        stopScanInternal()
    }

    private fun stopScanInternal() {
        scanJob?.cancel()
        scanJob = null
        try {
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (_: Exception) {
            // ignore
        }
        _isScanning.value = false
    }

    @Synchronized
    fun connect(address: String, name: String = "") {
        disconnect(manual = false)
        targetAddress = address
        shouldReconnect.set(true)
        openGatt()
    }

    @Synchronized
    fun disconnect(manual: Boolean = true) {
        if (manual) {
            shouldReconnect.set(false)
        }
        reconnectJob?.cancel()
        heartbeatJob?.cancel()
        connectionTimeoutJob?.cancel()
        waitingForMtu = false
        stopScanInternal()
        val oldGatt = gatt
        gatt = null // Ignore late callbacks from a closed connection.
        try {
            oldGatt?.disconnect()
            oldGatt?.close()
        } catch (_: Exception) {
            // ignore
        }
        gatt = null
        rxCharacteristic = null
        writeChunkSize = DEFAULT_CHUNK
        _connectionState.value = ConnectionState.DISCONNECTED
    }

    fun sendNavMessage(json: String): Boolean {
        if (_connectionState.value != ConnectionState.CONNECTED) return false
        val characteristic = rxCharacteristic ?: return false
        val gattLocal = gatt ?: return false
        // A leading newline also recovers the peripheral parser after a partial write.
        val payload = ("\n" + json.trimEnd() + "\n").toByteArray(Charsets.UTF_8)
        if (payload.size > 12288) {
            Log.w(TAG, "Navigation frame exceeds peripheral buffer: ${payload.size}")
            return false
        }
        lastQueuedAt = SystemClock.elapsedRealtime()
        // Finish the current JSON, then transmit only the most recent pending update.
        return outgoing.trySend(OutgoingFrame(gattLocal, characteristic, payload)).isSuccess
    }

    fun release() {
        disconnect(manual = true)
        outgoing.close()
        scope.cancel()
    }

    @Synchronized
    private fun openGatt() {
        if (targetAddress.isBlank() || !shouldReconnect.get()) return
        val device = try {
            adapter?.getRemoteDevice(targetAddress)
        } catch (_: IllegalArgumentException) {
            null
        } ?: run {
            _connectionState.value = ConnectionState.ERROR
            return
        }

        _connectionState.value = ConnectionState.CONNECTING
        writeChunkSize = DEFAULT_CHUNK
        waitingForMtu = false
        gatt = try {
            device.connectGatt(appContext, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } catch (error: SecurityException) {
            Log.e(TAG, "Bluetooth permission missing", error)
            _connectionState.value = ConnectionState.ERROR
            return
        }
        val openedGatt = gatt ?: run {
            _connectionState.value = ConnectionState.ERROR
            return
        }
        connectionTimeoutJob?.cancel()
        connectionTimeoutJob = scope.launch {
            delay(CONNECTION_TIMEOUT_MS)
            synchronized(this@BleNavClient) {
                    if (gatt === openedGatt && _connectionState.value != ConnectionState.CONNECTED) {
                        failConnection(openedGatt, "Connection setup timed out")
                    }
            }
        }
    }

    @Synchronized
    private fun failConnection(failedGatt: BluetoothGatt, reason: String) {
        if (gatt !== failedGatt) return
        Log.w(TAG, reason)
        disconnect(manual = false)
        _connectionState.value = ConnectionState.ERROR
        scheduleReconnect()
    }

    private fun discoverServices(activeGatt: BluetoothGatt) {
        Log.d(TAG, "Discovering NUS service")
        if (!activeGatt.discoverServices()) {
            failConnection(activeGatt, "Service discovery could not start")
        }
    }

    private fun scheduleReconnect() {
        if (!shouldReconnect.get()) return
        reconnectJob?.cancel()
        _connectionState.value = ConnectionState.RECONNECTING
        reconnectJob = scope.launch {
            delay(RECONNECT_DELAY_MS)
            if (shouldReconnect.get() && isActive) {
                openGatt()
            }
        }
    }

    private fun startHeartbeat() {
        heartbeatJob?.cancel()
        heartbeatJob = scope.launch {
            while (isActive && _connectionState.value == ConnectionState.CONNECTED) {
                delay(HEARTBEAT_INTERVAL_SEC * 1000)
                if (SystemClock.elapsedRealtime() - lastQueuedAt >= HEARTBEAT_INTERVAL_SEC * 1000) {
                    sendNavMessage(NavMessage.heartbeat())
                }
            }
        }
    }

    private suspend fun writeInChunks(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        payload: ByteArray
    ) {
        var offset = 0
        var busyRetries = 0
        while (offset < payload.size) {
            if (this.gatt !== gatt || _connectionState.value != ConnectionState.CONNECTED) return
            val end = minOf(offset + writeChunkSize, payload.size)
            val chunk = payload.copyOfRange(offset, end)
            val ok = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                gatt.writeCharacteristic(
                    characteristic,
                    chunk,
                    BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                ) == 0
            } else {
                @Suppress("DEPRECATION")
                run {
                    characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                    characteristic.value = chunk
                    gatt.writeCharacteristic(characteristic)
                }
            }
            if (!ok) {
                if (++busyRetries >= 25) {
                    Log.w(TAG, "BLE write busy; dropping partial frame and resynchronizing next frame")
                    return
                }
                delay(8)
                continue
            }
            busyRetries = 0
            offset = end
            delay(8)
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device ?: return
            val address = device.address ?: return
            val name = result.scanRecord?.deviceName
                ?: device.name
                ?: ""
            val hasNus = result.scanRecord?.serviceUuids?.any { it.uuid == SERVICE_UUID } == true
            val nameMatch = name.contains(DEVICE_NAME_HINT, ignoreCase = true)
            if (!hasNus && !nameMatch && name.isNotBlank()) {
                return
            }
            if (!hasNus && !nameMatch && name.isBlank()) {
                // Service filter scan already matched; keep unnamed devices
            }
            val displayName = name.ifBlank { "CYD $address" }
            deviceMap[address] = BleDeviceItem(address, displayName, result.rssi)
            _scannedDevices.value = deviceMap.values.sortedByDescending { it.rssi }
        }

        override fun onScanFailed(errorCode: Int) {
            _isScanning.value = false
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            synchronized(this@BleNavClient) {
                if (this@BleNavClient.gatt !== gatt) return
                Log.d(TAG, "Connection status=$status state=$newState")
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    failConnection(gatt, "GATT connection error: $status")
                    return
                }
                if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                    _connectionState.value = ConnectionState.CONNECTING
                    waitingForMtu = true
                    // Wait for onMtuChanged before starting another GATT operation.
                    if (!gatt.requestMtu(REQUESTED_MTU)) {
                        waitingForMtu = false
                        discoverServices(gatt) // MTU 23 / payload 20 remains usable.
                    }
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    failConnection(gatt, "Peripheral disconnected")
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            synchronized(this@BleNavClient) {
                if (this@BleNavClient.gatt !== gatt) return
                Log.d(TAG, "MTU=$mtu status=$status")
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    writeChunkSize = (mtu - 3).coerceIn(20, 500)
                }
                if (waitingForMtu) {
                    waitingForMtu = false
                    discoverServices(gatt)
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            synchronized(this@BleNavClient) {
                if (this@BleNavClient.gatt !== gatt) return
                Log.d(TAG, "Services status=$status")
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    failConnection(gatt, "Service discovery failed: $status")
                    return
                }
                val service = gatt.getService(SERVICE_UUID)
                val rx = service?.getCharacteristic(RX_UUID)
                val tx = service?.getCharacteristic(TX_UUID)
                if (rx == null || tx == null) {
                    failConnection(gatt, "NUS characteristics missing; check peripheral firmware")
                    return
                }
                rxCharacteristic = rx

                if (!gatt.setCharacteristicNotification(tx, true)) {
                    failConnection(gatt, "Could not enable local notifications")
                    return
                }
                val cccd = tx.getDescriptor(CCCD_UUID)
                if (cccd == null) {
                    failConnection(gatt, "NUS notification descriptor missing")
                    return
                }
                val accepted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        gatt.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == 0
                    } else {
                        @Suppress("DEPRECATION")
                        run {
                            cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                            gatt.writeDescriptor(cccd)
                        }
                    }
                if (!accepted) failConnection(gatt, "Notification subscription could not start")
            }
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            synchronized(this@BleNavClient) {
                if (this@BleNavClient.gatt !== gatt || descriptor.uuid != CCCD_UUID) return
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    failConnection(gatt, "Notification subscription failed: $status")
                    return
                }
                connectionTimeoutJob?.cancel()
                Log.i(TAG, "NUS connection ready")
                _connectionState.value = ConnectionState.CONNECTED
                startHeartbeat()
            }
        }
    }

    companion object {
        val SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val RX_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        val TX_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
        private val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        private const val DEVICE_NAME_HINT = "DisplayConnect"
        private const val REQUESTED_MTU = 512
        private const val DEFAULT_CHUNK = 20
        private const val CONNECTION_TIMEOUT_MS = 20_000L
        private const val TAG = "DisplayConnectBLE"
        private const val HEARTBEAT_INTERVAL_SEC = 15L
        private const val RECONNECT_DELAY_MS = 3000L
        private const val SCAN_DURATION_MS = 10_000L
    }
}
