package com.example.odbior_pomiarow

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.util.Log
import java.util.*
import android.content.SharedPreferences

@SuppressLint("MissingPermission")
object BleManager {
    //object - singleton, don't need to instantiate
    private const val TAG = "BLE_MANAGER"
    val SERVICE_UUID: UUID = UUID.fromString("12341234-5678-1234-5678-123412345678")
    val CONFIG_CHAR_UUID: UUID = UUID.fromString("87654321-4321-6789-4321-876543210987")
    val DATA_CHAR_UUID: UUID = UUID.fromString("78563412-7856-3412-7856-341278563412")
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    //Bufor przechowujący próbki od ostatniego zderzenia
    private val flightBuffer = mutableListOf<SampleData>()
    // Zabezpieczenie przed przepełnieniem RAMu (np. gdy piłka leży w bezruchu przez 10 minut)
    private const val MAX_BUFFER_SIZE = 12000 // ok. 2 minuty przy 100Hz
    var onHitEventReceivedListener: ((HistoryEntry) -> Unit)? = null

    private var sharedPrefs: SharedPreferences? = null

    private var configCharacteristic: BluetoothGattCharacteristic? = null
    private var dataCharacteristic: BluetoothGattCharacteristic? = null

    var onGpsDataReceivedListener: ((lat: Float, lon: Float, fix: Boolean) -> Unit)? = null
    var onDataSampleReceivedListener: ((h3x: Float, h3y: Float, h3z: Float, ax: Float, ay: Float, az: Float, gx: Float, gy: Float, gz: Float) -> Unit)? = null

    var latestBatteryVoltage: Float = 0.0f
    var latestBatteryPercent: Float = 0.0f
    var onBatteryDataUpdateMain: ((voltage: Float, percent: Float) -> Unit)? = null
    var onBatteryDataUpdateSettings: ((voltage: Float, percent: Float) -> Unit)? = null

    private var bluetoothAdapter: BluetoothAdapter? = null
    private var bluetoothGatt: BluetoothGatt? = null

    var isConnected = false
        private set

    var onDataReceivedListener: ((String) -> Unit)? = null
    var onConnectionStateChanged: ((Boolean) -> Unit)? = null

    fun init(context: Context) {
        val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothAdapter = bluetoothManager.adapter
        sharedPrefs = androidx.preference.PreferenceManager.getDefaultSharedPreferences(context.applicationContext)
    }

    fun startScanAndConnect(context: Context) {
        //I guarantee that variable is not null
        if (bluetoothAdapter == null || !bluetoothAdapter!!.isEnabled) {
            Log.e(TAG, "Bluetooth jest wyłączony lub niedostępny na tym urządzeniu!")
            return
        }

        val scanner = bluetoothAdapter?.bluetoothLeScanner
        if (scanner == null) {
            Log.e(TAG, "Brak dostępu do BluetoothLeScanner!")
            return
        }

        try {
            val filter = ScanFilter.Builder()
                .setDeviceName("SmartBall-Config")
                .build()

            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build()

            scanner.startScan(listOf(filter), settings, object : ScanCallback() {
                //funtions of ScanCallBack, because it is abstract class and I enlarge it
                override fun onScanResult(callbackType: Int, result: ScanResult?) {
                    val device = result?.device ?: return
                    Log.i(TAG, "Znaleziono urządzenie: ${device.name} [${device.address}]")
                    scanner.stopScan(this)
                    connectToDevice(context, device)
                }

                override fun onScanFailed(errorCode: Int) {
                    Log.e(TAG, "Błąd skanowania BLE: $errorCode")
                }
            })
        } catch (e: SecurityException) {
            Log.e(TAG, "Brak wymaganych uprawnień BLE: ${e.message}")
        }
    }

    private fun connectToDevice(context: Context, device: BluetoothDevice) {

        bluetoothGatt = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
            //autoconnect - on background
        } else {
            device.connectGatt(context, false, gattCallback)
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt?, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.i(TAG, "Połączono bezautoryzacyjnie z GATT! Żądamy MTU 512...")
                isConnected = true
                onConnectionStateChanged?.invoke(true)
                gatt?.requestMtu(512)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.w(TAG, "Rozłączono z BLE!")
                isConnected = false
                configCharacteristic = null
                dataCharacteristic = null
                onConnectionStateChanged?.invoke(false)
            }
        }


        override fun onMtuChanged(gatt: BluetoothGatt?, mtu: Int, status: Int) {
            Log.i(TAG, "MTU zmienione na: $mtu (status=$status). Rozpoczynam odkrywanie usług...")
            gatt?.discoverServices()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt?, status: Int) {
            Log.i(TAG, "onServicesDiscovered wywołane ze statusem: $status")
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt?.getService(SERVICE_UUID)
                if (service != null) {
                    configCharacteristic = service.getCharacteristic(CONFIG_CHAR_UUID)
                    dataCharacteristic = service.getCharacteristic(DATA_CHAR_UUID)

                    Log.i(TAG, "Odkryto charakterystyki! Rejestracja NOTIFY...")

                    dataCharacteristic?.let { char ->
                        val notifyResult = gatt.setCharacteristicNotification(char, true)
                        Log.d(TAG, "setCharacteristicNotification: $notifyResult")

                        val descriptor = char.getDescriptor(CCCD_UUID)
                        descriptor?.let { desc ->
                            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                                gatt.writeDescriptor(desc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                            } else {
                                @Suppress("DEPRECATION")
                                desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                                @Suppress("DEPRECATION")
                                gatt.writeDescriptor(desc)
                            }
                            Log.i(TAG, "Wysłano zapis do deskryptora CCCD (0x2902)!")
                            android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                                Log.i(TAG, "Wysyłam automatyczne zapytanie o konfigurację...")
                                readConfiguration()
                            }, 500)
                        }
                    }
                } else {
                    Log.e(TAG, "Nie znaleziono usługi o UUID: $SERVICE_UUID")
                }
            }
        }


        //receiving notify
        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(gatt: BluetoothGatt?, characteristic: BluetoothGattCharacteristic?) {
            @Suppress("DEPRECATION")
            val bytes = characteristic?.value ?: return
            handleIncomingData(bytes)
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            handleIncomingData(value)
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS && characteristic.uuid == CONFIG_CHAR_UUID) {
                val response = String(value)
                Log.d(TAG, "Odebrano tekst konfiguracyjny (READ): $response")
                parseAndSaveConfiguration(response)
                onDataReceivedListener?.invoke(response)
            }
        }
    }

    private fun parseAndSaveConfiguration(response: String) {
        if (response.startsWith("CFG:")) {
            val tokens = response.trim().split(":")
            if (tokens.size >= 5) {
                // Nadpisujemy pamięć telefonu tym, co przyszło z mikrokontrolera
                sharedPrefs?.edit()?.apply {
                    putString("ESP_WAKE_THS", tokens[1])
                    putString("ESP_SLEEP_THS", tokens[2])
                    putString("ESP_IDLE_TIME", tokens[3])
                    putString("ESP_HIT_THS", tokens[4])
                    apply()
                }

                // Aktualizujemy główny kalkulator w aplikacji
                tokens[4].toFloatOrNull()?.let { hitG ->
                    MetricsCalculator.updateHitThresholdFromBle(hitG)
                }
                Log.i(TAG, "Zsynchronizowano pamięć telefonu z fizycznymi ustawieniami ESP32!")
            }
        }
    }

    /**
     * Przetwarza dane pomiarowe spływające z ESP32 (Struktura global_data_t)
     */
    private fun handleIncomingData(bytes: ByteArray) {
        if (bytes.size < 40) {
            val text = String(bytes)
            parseAndSaveConfiguration(text)
            onDataReceivedListener?.invoke(text)
            return
        }

        val buffer = java.nio.ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.LITTLE_ENDIAN)

        try {
            val packetType = buffer.get() // 1 = Telemetria (Lot/Spoczynek), 2 = Zderzenie

            // --- ODCZYT CZUJNIKÓW ---
            val h3x = buffer.float; val h3y = buffer.float; val h3z = buffer.float
            val ax = buffer.float; val ay = buffer.float; val az = buffer.float
            val gx = buffer.float; val gy = buffer.float; val gz = buffer.float
            val lat = buffer.float; val lon = buffer.float
            val fix = buffer.get() != 0.toByte()

            if (buffer.remaining() >= 8) {
                latestBatteryVoltage = buffer.float
                latestBatteryPercent = buffer.float
                onBatteryDataUpdateMain?.invoke(latestBatteryVoltage, latestBatteryPercent)
                onBatteryDataUpdateSettings?.invoke(latestBatteryVoltage, latestBatteryPercent)
            }

            // ZAWSZE logujemy próbkę (do CSV na dysk i na żywy wykres)
            SessionManager.logSampleToCurrentSession(h3x, h3y, h3z, ax, ay, az, gx, gy, gz, lat, lon, fix)
            onDataSampleReceivedListener?.invoke(h3x, h3y, h3z, ax, ay, az, gx, gy, gz)
            onGpsDataReceivedListener?.invoke(lat, lon, fix)

            val sample = SampleData(ax, ay, az, gx, gy, gz, System.currentTimeMillis(), false)

            if (packetType == 1.toByte()) {
                // TRYB 1: Zbieramy dane do bufora
                if (flightBuffer.size < MAX_BUFFER_SIZE) {
                    flightBuffer.add(sample)
                }
            } else if (packetType == 2.toByte()) {
                // TRYB 2: ZDERZENIE - Wyzwalacz! Najpierw zamykamy poprzedni lot, potem logujemy uderzenie

                // --- A. ZAMKNIĘCIE I ANALIZA LOTU ---
                if (flightBuffer.isNotEmpty()) {
                    val hangTimeS = MetricsCalculator.calculateHangTimeSeconds(flightBuffer)

                    if (hangTimeS > 0.15f) { // Rejestrujemy tylko rzeczywisty lot (> 150ms nieważkości)
                        val maxSpinDps = flightBuffer.maxOfOrNull {
                            kotlin.math.sqrt((it.gx * it.gx + it.gy * it.gy + it.gz * it.gz).toDouble()).toFloat()
                        } ?: 0f

                        val flightDurationMs = (hangTimeS * 1000).toLong()
                        val flightEntry = HistoryEntry(
                            type = EntryType.FLIGHT,
                            date = java.util.Date(System.currentTimeMillis() - flightDurationMs),
                            durationMs = flightDurationMs,
                            samples = flightBuffer.toList(), // Przekazujemy CAŁĄ historię lotu!
                            peakValue = maxSpinDps
                        )
                        HistoryCacheManager.addEvent(flightEntry)
                        onHitEventReceivedListener?.invoke(flightEntry)
                    }
                    flightBuffer.clear() // Zderzenie resetuje bufor
                }

                // --- B. REJESTRACJA UDERZENIA ---
                val hitMagG = kotlin.math.sqrt((h3x * h3x + h3y * h3y + h3z * h3z).toDouble()).toFloat()
                val hitEntry = HistoryEntry(
                    type = EntryType.HIT,
                    date = java.util.Date(),
                    durationMs = 150, // Można to w przyszłości też wyciągnąć z pakietu
                    samples = listOf(sample),
                    peakValue = hitMagG * 1000f // Mnożymy przez 1000 (jednostki mg w reszcie apki)
                )
                HistoryCacheManager.addEvent(hitEntry)
                onHitEventReceivedListener?.invoke(hitEntry)
            }
        } catch (e: Exception) {
            Log.e("SMART_BALL", "BLE BŁĄD: ${e.message}", e)
        }
    }

    // Wysyła całą konfigurację z telefonu do ESP przed treningiem
    fun applyMasterConfiguration(sharedPrefs: SharedPreferences) {
        if (!isConnected) return

        // Przelatujemy przez WSZYSTKIE zapisane ustawienia w telefonie
        sharedPrefs.all.forEach { (key, value) ->
            // Jeśli klucz zaczyna się od "ESP_", wiemy, że to parametr dla mikrokontrolera
            if (key.startsWith("ESP_")) {
                // Usuwamy "ESP_" z nazwy klucza. Zostaje nam np. "HIT_THS"
                val commandName = key.removePrefix("ESP_")

                // Tworzymy dynamiczną komendę i ją wysyłamy, np. CMD:HIT_THS:12.5
                sendCommand("CMD:$commandName:$value")
            }
        }
    }
    /*
     * Odczytuje aktualną konfigurację z ESP32
     */
    fun readConfiguration() {
        val char = configCharacteristic ?: return
        bluetoothGatt?.readCharacteristic(char)
    }

    /**
     * Wysyła komendę tekstową do ESP32
     */
    fun sendCommand(command: String): Boolean {
        val gatt = bluetoothGatt ?: return false
        val char = configCharacteristic ?: return false

        val bytes = command.toByteArray(Charsets.UTF_8)

        return if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            gatt.writeCharacteristic(char, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            char.value = bytes
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(char)
        }
    }

    fun disconnect() {
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        isConnected = false
    }
}