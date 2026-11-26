package com.example.odbior_pomiarow

import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import kotlinx.coroutines.*
import org.eclipse.paho.client.mqttv3.*
import org.json.JSONObject
import org.osmdroid.config.Configuration
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker
import org.osmdroid.views.overlay.CopyrightOverlay
import org.osmdroid.tileprovider.tilesource.XYTileSource

class LocationActivity : AppCompatActivity() {

    private lateinit var mqttClient: MqttClient
    private val BROKER_URI = "tcp://broker.emqx.io:1883"
    private val MQTT_LOCATION_TOPIC = "esp32/device/location"

    private lateinit var mapView: MapView
    private lateinit var tvCoordinates: TextView
    private lateinit var btnBuzzer: Button

    private var espMarker: Marker? = null
    private val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())
    private var isBuzzerOn = false

    // Zapamiętujemy poprzedni listener stanu połączenia, aby nie nadpisać logiki z innych ekranów (np. MainActivity)
    private var previousConnectionListener: ((Boolean) -> Unit)? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val ctx = applicationContext

        Configuration.getInstance().load(ctx, android.preference.PreferenceManager.getDefaultSharedPreferences(ctx))
        Configuration.getInstance().userAgentValue =
            "SmartBallApp/1.0 (com.example.odbior_pomiarow; contact: twoj@email.pl)"
        setContentView(R.layout.activity_location)

        mapView = findViewById(R.id.mapView)
        tvCoordinates = findViewById(R.id.tvCoordinates)
        btnBuzzer = findViewById(R.id.btnTriggerBuzzer)

        // Klucz API CARTO
        val cartoApiKey = "cb1_5fvb_1_813690c9312b78b0d14693b9"

        // Konfiguracja źródła mapy CARTO
        val cartoTileSource = object : XYTileSource(
            "CARTO-Voyager",
            0,
            19,
            256,
            ".png",
            arrayOf(
                "https://basemaps.cartocdn.com/rastertiles/voyager/"
            ),
            "© OpenStreetMap contributors © CARTO"
        ) {
            override fun getTileURLString(pMapTileIndex: Long): String {
                return super.getTileURLString(pMapTileIndex) +
                        "?key=$cartoApiKey"
            }
        }

        mapView.setTileSource(cartoTileSource)
        mapView.setUseDataConnection(true)
        mapView.setMultiTouchControls(true)

        mapView.overlays.add(CopyrightOverlay(this))

        mapView.controller.setZoom(18.0)
        mapView.controller.setCenter(GeoPoint(52.0, 19.0))

        // 1. Natychmiastowe wyświetlenie ostatniej pozycji po wejściu na ekran (bez czekania na nowy pakiet)
        if (BleManager.isConnected && (BleManager.latestLat != 0.0f || BleManager.latestLon != 0.0f)) {
            updateMapPosition(BleManager.latestLat, BleManager.latestLon, BleManager.latestFix, source = "BLE")
        }

        // 2. Nasłuch na żywo dla LocationActivity
        BleManager.onGpsDataUpdateLocation = { lat, lon, fix ->
            runOnUiThread {
                if (BleManager.isConnected) {
                    updateMapPosition(lat, lon, fix, source = "BLE")
                }
            }
        }

        // 2. Reakcja na zmianę stanu połączenia BLE w czasie rzeczywistym
        previousConnectionListener = BleManager.onConnectionStateChanged
        BleManager.onConnectionStateChanged = { connected ->
            previousConnectionListener?.invoke(connected)
            runOnUiThread {
                handleBleStateChange(connected)
            }
        }

        val btnBack = findViewById<Button>(R.id.btnBackFromLocation)
        mapView.setMultiTouchControls(true)
        mapView.controller.setZoom(18.0)
        mapView.controller.setCenter(GeoPoint(52.0, 19.0))
        btnBack.setOnClickListener { finish() }

        btnBuzzer.setOnClickListener {
            isBuzzerOn = !isBuzzerOn
            if (isBuzzerOn) {
                btnBuzzer.text = "WYŁĄCZ GŁOŚNIK (BUZER)"
                btnBuzzer.setBackgroundColor(Color.BLACK)
                manageBuzzerState(true)
            } else {
                btnBuzzer.text = "WŁĄCZ GŁOŚNIK (BUZER)"
                btnBuzzer.setBackgroundColor(Color.parseColor("#D32F2F"))
                manageBuzzerState(false)
            }
        }

        // Inicjalizujemy MQTT (przyda się od razu jeśli brak BLE, lub w razie jego zerwania + do obsługi buzera)
        setupMqttClient()
    }

    private fun handleBleStateChange(isBleConnected: Boolean) {
        if (isBleConnected) {
            Toast.makeText(this, "Połączono BLE – przełączono lokalizację na BLE", Toast.LENGTH_SHORT).show()
            // Opcjonalnie: odsubskrybuj lokalizację z MQTT, żeby nie zużywać pakietów w tle
            toggleMqttLocationSubscription(subscribe = false)
        } else {
            Toast.makeText(this, "Rozłączono BLE – przełączono lokalizację na GSM", Toast.LENGTH_SHORT).show()
            tvCoordinates.text = "Utracono BLE. Oczekiwanie na pakiety z GSM..."
            toggleMqttLocationSubscription(subscribe = true)
        }
    }

    override fun onResume() {
        super.onResume()
        mapView.onResume()
    }

    override fun onPause() {
        mapView.onPause()
        super.onPause()
    }

    private fun toggleMqttLocationSubscription(subscribe: Boolean) {
        scope.launch(Dispatchers.IO) {
            try {
                if (::mqttClient.isInitialized && mqttClient.isConnected) {
                    if (subscribe) {
                        mqttClient.subscribe(MQTT_LOCATION_TOPIC, 0)
                        Log.d("SMART_BALL", "MQTT: Zasubskrybowano lokalizację GSM")
                    } else {
                        mqttClient.unsubscribe(MQTT_LOCATION_TOPIC)
                        Log.d("SMART_BALL", "MQTT: Odsubskrybowano lokalizację GSM (aktywne BLE)")
                    }
                }
            } catch (e: Exception) {
                Log.e("SMART_BALL", "Błąd zmiany subskrypcji MQTT", e)
            }
        }
    }

    private fun setupMqttClient() {
        scope.launch(Dispatchers.IO) {
            try {
                System.setProperty("java.net.preferIPv4Stack", "true")

                val clientId = MqttClient.generateClientId()
                mqttClient = MqttClient(BROKER_URI, clientId, org.eclipse.paho.client.mqttv3.persist.MemoryPersistence())

                mqttClient.setCallback(object : MqttCallback {
                    override fun connectionLost(cause: Throwable?) {
                        Log.e("SMART_BALL", "MQTT: Połączenie zerwane!", cause)
                    }

                    override fun messageArrived(topic: String?, message: MqttMessage?) {
                        // KLUCZOWY WARUNEK: Jeśli BLE jest połączone, całkowicie ignorujemy lokalizację z GSM
                        if (BleManager.isConnected) {
                            return
                        }

                        val payload = String(message?.payload ?: ByteArray(0))
                        if (topic == MQTT_LOCATION_TOPIC && message != null) {
                            runOnUiThread {
                                // Podwójne sprawdzenie w wątku UI na wypadek wyścigu wątków
                                if (BleManager.isConnected) return@runOnUiThread

                                try {
                                    val json = JSONObject(payload)
                                    val lat = json.getDouble("lat").toFloat()
                                    val lon = json.getDouble("lon").toFloat()
                                    val fix = json.getInt("fix") == 1

                                    updateMapPosition(lat, lon, fix, source = "GSM")
                                } catch (e: Exception) {
                                    Log.e("SMART_BALL", "MQTT Błąd parsowania JSON-a!", e)
                                }
                            }
                        }
                    }

                    override fun deliveryComplete(token: IMqttDeliveryToken?) {}
                })

                val options = MqttConnectOptions().apply {
                    isCleanSession = true
                    isAutomaticReconnect = true // Warto dodać automatyczne wznawianie przy zaniku internetu
                }
                mqttClient.connect(options)

                // Subskrybujemy temat lokalizacji TYLKO wtedy, gdy w momencie połączenia z brokerem nie ma BLE
                if (!BleManager.isConnected) {
                    mqttClient.subscribe(MQTT_LOCATION_TOPIC, 0)
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@LocationActivity, "Brak BLE – aktywna lokalizacja GSM", Toast.LENGTH_SHORT).show()
                    }
                }
            } catch (e: Exception) {
                Log.e("SMART_BALL", "Błąd inicjalizacji MQTT", e)
            }
        }
    }

    private fun updateMapPosition(lat: Float, lon: Float, fix: Boolean, source: String) {
        if (lat == 0.0f && lon == 0.0f) {
            tvCoordinates.text = "[$source] Oczekiwanie na poprawne współrzędne GPS..."
            return
        }

        tvCoordinates.text = String.format(
            "Źródło: %s\nSzerokość: %.5f°\nDługość: %.5f°\nStatus FIX: %s",
            source,
            lat,
            lon,
            if (fix) "ZABLOKOWANY (Dobre dane)" else "BRAK FIX (Słaby sygnał)"
        )

        val espPoint = GeoPoint(lat.toDouble(), lon.toDouble())
        if (espMarker == null) {
            espMarker = Marker(mapView).apply {
                title = "Lokalizacja ESP32 ($source)"
                setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_BOTTOM)
            }
            mapView.overlays.add(espMarker)
        } else {
            espMarker?.title = "Lokalizacja ESP32 ($source)"
        }

        espMarker?.position = espPoint
        mapView.controller.setCenter(espPoint)
        mapView.invalidate()
    }

    private fun manageBuzzerState(turnOn: Boolean) {
        scope.launch(Dispatchers.IO) {
            if (SettingsActivity.isBleConnected()) {
                val success = SettingsActivity.sendSoundCommandBle(turnOn)
                withContext(Dispatchers.Main) {
                    if (success) {
                        val mode = if (turnOn) "włączony" else "wyłączony"
                        Toast.makeText(applicationContext, "Głośnik $mode przez BLE!", Toast.LENGTH_SHORT).show()
                    } else {
                        sendBuzzerRequestMqtt(turnOn)
                    }
                }
            } else {
                withContext(Dispatchers.Main) {
                    sendBuzzerRequestMqtt(turnOn)
                }
            }
        }
    }

    private fun sendBuzzerRequestMqtt(turnOn: Boolean) {
        if (::mqttClient.isInitialized && mqttClient.isConnected) {
            val messageText = if (turnOn) "PLAY" else "STOP"
            val message = MqttMessage(messageText.toByteArray()).apply { qos = 1 }
            mqttClient.publish("esp32/device/audio", message)
            Toast.makeText(this, "Brak BLE. Wysłano przez GSM (MQTT)", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Brak połączenia BLE oraz sieciowego z brokerem GSM!", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        // Przywracamy poprzedni listener i odpinamy referencję do tej Activity
        BleManager.onGpsDataUpdateLocation = null
        BleManager.onConnectionStateChanged = previousConnectionListener

        mapView.onDetach()

        scope.cancel()
        try {
            if (::mqttClient.isInitialized && mqttClient.isConnected) {
                mqttClient.disconnect()
                mqttClient.close()
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }
}