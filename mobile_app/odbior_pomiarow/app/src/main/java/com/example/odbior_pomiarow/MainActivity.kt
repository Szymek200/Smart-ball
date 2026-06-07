package com.example.odbior_pomiarow

import android.content.Context
import android.graphics.Color
import android.net.wifi.WifiManager
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import android.content.Intent
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import com.github.mikephil.charting.formatter.ValueFormatter
import kotlinx.coroutines.*
import java.io.File
import java.io.FileWriter
import java.io.InputStream
import java.net.ServerSocket
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.*
import kotlin.math.sqrt

class MainActivity : AppCompatActivity() {

    companion object {
        // Lista statyczna dostępna dla HistoryActivity
        val fullHitHistory = mutableListOf<HistoryEntry>()
    }

    // Stałe konfiguracyjne
    private val TCP_PORT = 3333               // Port zgodny z ESP32
    private val LOG_TAG = "TCP_LOGGER"
    private val LOG_FILE_PREFIX = "pomiary"
    private val UI_UPDATE_INTERVAL_MS = 50L
    private val HEARTBEAT_TIMEOUT_MS = 5000L

    // Rozmiar struktury global_data_t z ESP32 (45 bajtów)
    private val STRUCT_SIZE = 45

    // --- ELEMENTY UI (WIDOKI) ---
    private lateinit var collisionChart: LineChart
    private lateinit var flightChart: LineChart
    private lateinit var lastHitTextView: TextView
    private lateinit var statusTextView: TextView
    private lateinit var connectionStatusTextView: TextView

    // Pola tekstowe dla Akcelerometru Uderzeniowego H3LIS
    private lateinit var h3AccelX: TextView; private lateinit var h3AccelY: TextView; private lateinit var h3AccelZ: TextView

    // Pola tekstowe dla Akcelerometru IMU
    private lateinit var imuAccelX: TextView; private lateinit var imuAccelY: TextView; private lateinit var imuAccelZ: TextView

    // Pola tekstowe dla Żyroskopu IMU

    // Pola tekstowe dla GPS
    private lateinit var gpsLat: TextView; private lateinit var gpsLon: TextView; private lateinit var gpsFix: TextView

    private lateinit var accelX: TextView; private lateinit var accelY: TextView; private lateinit var accelZ: TextView
    private lateinit var gyroX: TextView; private lateinit var gyroY: TextView; private lateinit var gyroZ: TextView

    private lateinit var hitHistoryContainer: LinearLayout
    private lateinit var logSwitch: Switch

    // --- BUFORY I STANY (ZDEKLAROWANE TYLKO RAZ) ---
    private val eventSamples = mutableListOf<SampleData>()          // Bufor na próbki uderzenia
    private val currentIntervalSamples = mutableListOf<SampleData>() // Bufor na próbki lotu (wsteczna historia)

    private var receiveState = ReceiveState.NORMAL
    private val TOTAL_EVENT_SAMPLES = 200              // 100 przed i 100 po zderzeniu

    private var lastUiUpdateTime = 0L
    private var lastPacketTime = 0L

    // Współbieżność Coroutines
    private val scope = CoroutineScope(Dispatchers.IO)
    private var tcpServerJob: Job? = null
    private var clientSocket: Socket? = null

    // Logowanie do pliku CSV
    private var logWriter: FileWriter? = null
    private var isLoggingEnabled = false
    private var logFile: File? = null

    private val logFileName: String
        get() = "${LOG_FILE_PREFIX}_${SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())}.csv"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.activity_main)

        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main)) { v, insets ->
            val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
            insets
        }

        initializeViews()
        setupChart()
        setupFlightChart()

        startTcpServer()
        startConnectionStatusChecker()
    }

    // --- URUCHOMIENIE SERWERA TCP ---
    private fun startTcpServer() { // Pozostawiamy starą nazwę metody, żeby nie psuć wywołań w onCreate
        tcpServerJob = scope.launch {
            val espIpAddress = "192.168.4.1" // Stały, domyślny IP dla SoftAP w ESP-IDF

            while (isActive) {
                try {
                    Log.i(LOG_TAG, "Próba połączenia z ESP32 pod adresem $espIpAddress:$TCP_PORT...")
                    runOnUiThread {
                        statusTextView.text = "Status: Łączenie z urządzeniem..."
                    }

                    // Tworzymy gniazdo klienta — ta linia próbuje połączyć się z ESP32
                    clientSocket = Socket(espIpAddress, TCP_PORT)

                    Log.i(LOG_TAG, "Połączono pomyślnie z ESP32!")
                    updateHeartbeat()

                    // Obsługa strumienia danych (blokuje wątek dopóki połączenie trwa)
                    handleClientStream(clientSocket!!)

                } catch (e: Exception) {
                    Log.w(LOG_TAG, "Nie udało się połączyć z ESP32: ${e.message}. Ponowna próba za 3 sekundy...")
                    runOnUiThread {
                        statusTextView.text = "Status: Oczekiwanie na urządzenie..."
                    }
                    delay(3000L) // Odczekaj 3 sekundy przed kolejną próbą nawiązania sesji
                }
            }
        }
    }

    private suspend fun handleClientStream(socket: Socket) {
        withContext(Dispatchers.IO) {
            val inputStream = socket.getInputStream()
            val buffer = ByteArray(STRUCT_SIZE)

            try {
                // Czytamy ze strumienia dopóki połączenie nie zostanie przerwane
                while (isActive && !socket.isClosed && socket.isConnected) {
                    readFully(inputStream, buffer)
                    updateHeartbeat()

                    val byteBuffer = ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN)
                    parseTcpStruct(byteBuffer)
                }
            } catch (e: Exception) {
                Log.w(LOG_TAG, "Błąd transmisji lub rozłączenie: ${e.message}")
            } finally {
                try { socket.close() } catch (e: Exception) {}
                runOnUiThread {
                    connectionStatusTextView.text = "Status: Rozłączono"
                    connectionStatusTextView.setBackgroundColor(Color.parseColor("#FFB3B3"))
                }
            }
        }
    }

    private fun readFully(inputStream: InputStream, buffer: ByteArray) {
        var offset = 0
        var bytesToRead = buffer.size
        while (bytesToRead > 0) {
            val bytesRead = inputStream.read(buffer, offset, bytesToRead)
            if (bytesRead == -1) throw Exception("Koniec strumienia TCP")
            offset += bytesRead
            bytesToRead -= bytesRead
        }
    }

    // --- PARSER STRUMIENIA TCP ---
    private fun parseTcpStruct(buffer: ByteBuffer) {
        buffer.position(0)

        // 1. Accel H3LIS (Pobieramy czyste wartości G przysłane z ESP32)
        val h3_ax = buffer.getFloat()
        val h3_ay = buffer.getFloat()
        val h3_az = buffer.getFloat()

        // 2. Accel IMU
        val imu_ax = buffer.getFloat()
        val imu_ay = buffer.getFloat()
        val imu_az = buffer.getFloat()

        // 3. Gyro IMU
        val imu_gx = buffer.getFloat()
        val imu_gy = buffer.getFloat()
        val imu_gz = buffer.getFloat()

        // 4. GPS
        val lat = buffer.getFloat()
        val lon = buffer.getFloat()
        val gpsFix = buffer.get() != 0.toByte()

        val ts = System.currentTimeMillis()

        // Do rysowania wykresu lotu i wyświetlania na ekranie konwertujemy IMU na jednostki [mg] (mnożymy * 1000)
        val currentSample = SampleData(
            imu_ax * 1000f, imu_ay * 1000f, imu_az * 1000f,
            imu_gx, imu_gy, imu_gz, ts
        )

        // Zapis do pliku CSV (zapisujemy czyste, czytelne wartości)
        if (isLoggingEnabled) {
            writeSampleToCsv(h3_ax, h3_ay, h3_az, imu_ax * 1000f, imu_ay * 1000f, imu_az * 1000f, imu_gx, imu_gy, imu_gz, lat, lon, gpsFix)
        }

        // ====================================================================
        // POPRAWIONA DETEKCJA ZDERZENIA BAZUJĄCA NA AKCELEROMETRZE 400G
        // ====================================================================
        // Wypadkowa siła G (wektor 3D) wyliczona bezpośrednio z wartości G
        val totalG = sqrt((h3_ax * h3_ax + h3_ay * h3_ay + h3_az * h3_az).toDouble()).toFloat()

        if (receiveState == ReceiveState.NORMAL) {
            // Na ekranie telefonu pokazujemy wartości w mg
            displaySensorData(
                h3_ax, h3_ay, h3_az,
                imu_ax * 1000f, imu_ay * 1000f, imu_az * 1000f,
                imu_gx, imu_gy, imu_gz,
                lat, lon, gpsFix
            )
            analyzeFlight(currentSample)

            // Jeśli wypadkowe przeciążenie na czujniku uderzeniowym przekroczy próg 4.5G -> aktywuj zderzenie
            // Gdy urządzenie leży, totalG wynosi około 1.0f (grawitacja ziemska).
            if (totalG > 4.5f) {
                triggerEventTransition()
            }
        } else if (receiveState == ReceiveState.EVENT_RECEIVING) {
            eventSamples.add(currentSample.copy(isLive = false))

            if (eventSamples.size >= TOTAL_EVENT_SAMPLES) {
                finalizeEventProcessing()
            }
        }
    }

    private fun triggerEventTransition() {
        receiveState = ReceiveState.EVENT_RECEIVING
        eventSamples.clear()

        // Przepisz ostatnie 100 pomiarów z lotu jako czas "PRZED" uderzeniem
        val preHistoryCount = minOf(currentIntervalSamples.size, 100)
        if (preHistoryCount > 0) {
            val preSamples = currentIntervalSamples.takeLast(preHistoryCount)
            eventSamples.addAll(preSamples.map { it.copy(isLive = false) })
        }
        currentIntervalSamples.clear()

        runOnUiThread {
            statusTextView.text = "!!! WYKRYTO ZDERZENIE: POBIERANIE PACZKI !!!"
            statusTextView.setTextColor(Color.RED)
        }
    }

    private fun finalizeEventProcessing() {
        val samplesToDraw = ArrayList(eventSamples)
        receiveState = ReceiveState.NORMAL
        currentIntervalSamples.clear()

        if (samplesToDraw.isEmpty()) return

        val duration = samplesToDraw.size * 10L // 200 pomiarów * 10ms delay na ESP = 2 sekundy
        val peakForce = samplesToDraw.maxOf {
            sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()).toFloat()
        }

        runOnUiThread {
            addHitToHistory(Date(), duration, peakForce, samplesToDraw)
            drawCollisionChart(samplesToDraw)
            statusTextView.text = "Zderzenie przetworzone pomyślnie (200 próbek)"
            statusTextView.setTextColor(Color.BLACK)
        }
        eventSamples.clear()
    }

    private fun analyzeFlight(sample: SampleData) {
        currentIntervalSamples.add(sample)
        if (currentIntervalSamples.size > 300) {
            currentIntervalSamples.removeAt(0)
        }
        if (currentIntervalSamples.size % 100 == 0) {
            processFlightData()
        }
    }

    private fun processFlightData() {
        if (currentIntervalSamples.size < 10) return
        val flightSegment = ArrayList(currentIntervalSamples)

        val avgRotation = flightSegment.map {
            sqrt((it.gx * it.gx + it.gy * it.gy + it.gz * it.gz).toDouble()).toFloat()
        }.average().toFloat()

        runOnUiThread {
            drawFlightChart(flightSegment)
            lastHitTextView.text = String.format("Lot na żywo... Śr. obrót: %.1f dps", avgRotation)
            lastHitTextView.setBackgroundColor(Color.CYAN)
        }
    }

    // --- WIZUALIZACJA I WYKRESY ---
    private fun drawCollisionChart(samples: List<SampleData>) {
        val accelEntries = mutableListOf<Entry>()
        val gyroEntries = mutableListOf<Entry>()
        if (samples.isEmpty()) return

        samples.forEachIndexed { index, s ->
            val x = index * 0.010f // Każda próbka to 10ms

            // s.ax jest już w miligrawitacjach [mg], więc dzielimy przez 1000f, aby na wykresie mieć czyste jednostki G
            val aMag = sqrt((s.ax * s.ax + s.ay * s.ay + s.az * s.az).toDouble()).toFloat() / 1000f
            accelEntries.add(Entry(x, aMag))

            val gMag = sqrt((s.gx * s.gx + s.gy * s.gy + s.gz * s.gz).toDouble()).toFloat()
            gyroEntries.add(Entry(x, gMag))
        }

        val setA = LineDataSet(accelEntries, "Siła [G]").apply {
            color = Color.RED; axisDependency = com.github.mikephil.charting.components.YAxis.AxisDependency.LEFT
            setDrawCircles(false); lineWidth = 2.5f; setDrawValues(false)
        }
        val setG = LineDataSet(gyroEntries, "Rotacja [dps]").apply {
            color = Color.BLUE; axisDependency = com.github.mikephil.charting.components.YAxis.AxisDependency.RIGHT
            setDrawCircles(false); lineWidth = 2.5f; setDrawValues(false)
        }

        collisionChart.data = LineData(setA, setG)
        collisionChart.invalidate()
    }

    private fun drawFlightChart(samples: List<SampleData>) {
        if (samples.isEmpty()) return
        val gyroEntries = ArrayList<Entry>()

        samples.forEachIndexed { index, s ->
            val timeSec = index * 0.010f
            val gMag = sqrt((s.gx * s.gx + s.gy * s.gy + s.gz * s.gz).toDouble()).toFloat()
            gyroEntries.add(Entry(timeSec, gMag))
        }

        val setG = LineDataSet(gyroEntries, "Rotacja [dps]").apply {
            color = Color.BLUE; lineWidth = 2f; setDrawCircles(false); setDrawValues(false)
            mode = LineDataSet.Mode.CUBIC_BEZIER; setDrawFilled(true); fillAlpha = 50; fillColor = Color.BLUE
        }

        flightChart.data = LineData(setG)
        flightChart.invalidate()
    }

    private fun displaySensorData(
        h3x: Float, h3y: Float, h3z: Float,
        ax: Float, ay: Float, az: Float,
        gx: Float, gy: Float, gz: Float,
        lat: Float, lon: Float, fix: Boolean
    ) {
        val now = System.currentTimeMillis()
        if (now - lastUiUpdateTime < UI_UPDATE_INTERVAL_MS) return // Ograniczenie narzutu na UI
        lastUiUpdateTime = now

        runOnUiThread {
            // 1. Aktualizacja H3LIS331DL (wartości przekazywane są w G)
            h3AccelX.text = String.format("X: %.2f G", h3x)
            h3AccelY.text = String.format("Y: %.2f G", h3y)
            h3AccelZ.text = String.format("Z: %.2f G", h3z)

            // 2. Aktualizacja Akcelerometru IMU (konwertujemy na mg dla precyzji profilu)
            imuAccelX.text = String.format("X: %.1f mg", ax)
            imuAccelY.text = String.format("Y: %.1f mg", ay)
            imuAccelZ.text = String.format("Z: %.1f mg", az)

            // 3. Aktualizacja Żyroskopu IMU (w dps)
            gyroX.text = String.format("X: %.1f dps", gx)
            gyroY.text = String.format("Y: %.1f dps", gy)
            gyroZ.text = String.format("Z: %.1f dps", gz)

            // 4. Aktualizacja sekcji GPS
            gpsLat.text = String.format("Szer: %.5f°", lat)
            gpsLon.text = String.format("Dług: %.5f°", lon)
            if (fix) {
                gpsFix.text = "FIX: TAK"
                gpsFix.setTextColor(Color.parseColor("#006400")) // Ciemnozielony
            } else {
                gpsFix.text = "FIX: BRAK"
                gpsFix.setTextColor(Color.RED)
            }

            statusTextView.text = "Status: Odbieram dane..."
            statusTextView.setTextColor(Color.BLACK)
        }
    }

    private fun addHitToHistory(hitDate: Date, durationMs: Long, peakForce: Float, currentSamples: List<SampleData>) {
        val newHit = HistoryEntry(EntryType.HIT, hitDate, durationMs, currentSamples, peakValue = peakForce)
        fullHitHistory.add(0, newHit)

        runOnUiThread {
            val forceInG = peakForce / 1000f
            lastHitTextView.text = String.format("Ostatnie: %.2f G (%d ms)", forceInG, durationMs)
            lastHitTextView.setBackgroundColor(Color.parseColor("#FFD700"))

            val quickSummary = TextView(this).apply {
                text = String.format("💥 %.2f G | %d ms", forceInG, durationMs)
                textSize = 14f
            }
            hitHistoryContainer.addView(quickSummary, 0)
        }
    }

    // --- MONITOR POŁĄCZENIA (HEARTBEAT) ---
    private fun updateHeartbeat() {
        lastPacketTime = System.currentTimeMillis()
        runOnUiThread {
            connectionStatusTextView.text = "Status: Połączono"
            connectionStatusTextView.setTextColor(Color.BLACK)
            connectionStatusTextView.setBackgroundColor(Color.parseColor("#B3FFB3"))
        }
    }

    private fun startConnectionStatusChecker() {
        scope.launch(Dispatchers.Main) {
            while (isActive) {
                delay(1000L)
                if (System.currentTimeMillis() - lastPacketTime > HEARTBEAT_TIMEOUT_MS) {
                    connectionStatusTextView.text = "Status: Rozłączono"
                    connectionStatusTextView.setTextColor(Color.RED)
                    connectionStatusTextView.setBackgroundColor(Color.parseColor("#FFB3B3"))
                }
            }
        }
    }

    // --- INICJALIZACJA UI I ZAPISU CSV ---
    private fun initializeViews() {
        findViewById<Button>(R.id.btnSettings).setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }
        findViewById<Button>(R.id.btnShowHistory).setOnClickListener {
            startActivity(Intent(this, HistoryActivity::class.java))
        }

        lastHitTextView = findViewById(R.id.lastHitTextView)
        collisionChart = findViewById(R.id.collisionChart)
        flightChart = findViewById(R.id.flightChart)
        statusTextView = findViewById(R.id.statusTextView)
        connectionStatusTextView = findViewById(R.id.connectionStatusTextView)

        // 1. Akcelerometr Uderzeniowy H3LIS
        h3AccelX = findViewById(R.id.h3AccelX)
        h3AccelY = findViewById(R.id.h3AccelY)
        h3AccelZ = findViewById(R.id.h3AccelZ)

        // 2. Akcelerometr IMU
        imuAccelX = findViewById(R.id.imuAccelX)
        imuAccelY = findViewById(R.id.imuAccelY)
        imuAccelZ = findViewById(R.id.imuAccelZ)

        // 3. Żyroskop IMU
        gyroX = findViewById(R.id.gyroX)
        gyroY = findViewById(R.id.gyroY)
        gyroZ = findViewById(R.id.gyroZ)

        // 4. GPS
        gpsLat = findViewById(R.id.gpsLat)
        gpsLon = findViewById(R.id.gpsLon)
        gpsFix = findViewById(R.id.gpsFix)



        hitHistoryContainer = findViewById(R.id.hitHistoryContainer)
        logSwitch = findViewById(R.id.logSwitch)

        logSwitch.setOnCheckedChangeListener { _, isChecked ->
            if (isChecked) initLogFile()
            else {
                isLoggingEnabled = false
                logWriter?.close()
                logWriter = null
                Log.i(LOG_TAG, "Logowanie CSV wyłączone.")
            }
        }
    }

    private fun initLogFile() {
        val appDirectory = getExternalFilesDir(null) ?: return
        logFile = File(appDirectory, logFileName)
        try {
            logWriter = FileWriter(logFile, true)
            // Zapisz nagłówek jeśli plik jest pusty
            logWriter?.write("Timestamp,H3_AX,H3_AY,H3_AZ,IMU_AX,IMU_AY,IMU_AZ,IMU_GX,IMU_GY,IMU_GZ,Lat,Lon,Fix\n")
            isLoggingEnabled = true
            Toast.makeText(this, "Zapis do: ${logFile?.name}", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            isLoggingEnabled = false
            logSwitch.isChecked = false
        }
    }

    private fun writeSampleToCsv(h3x: Float, h3y: Float, h3z: Float, ax: Float, ay: Float, az: Float, gx: Float, gy: Float, gz: Float, lat: Float, lon: Float, fix: Boolean) {
        scope.launch(Dispatchers.IO) {
            try {
                logWriter?.write("${System.currentTimeMillis()},$h3x,$h3y,$h3z,$ax,$ay,$az,$gx,$gy,$gz,$lat,$lon,${if(fix) 1 else 0}\n")
                logWriter?.flush()
            } catch (e: Exception) {
                Log.e(LOG_TAG, "Błąd zapisu wiersza CSV: ${e.message}")
            }
        }
    }

    private fun setupChart() {
        collisionChart.apply {
            description.isEnabled = false; setTouchEnabled(true); isDragEnabled = true
            setScaleEnabled(true); setPinchZoom(true); setBackgroundColor(Color.WHITE)
            xAxis.apply {
                position = com.github.mikephil.charting.components.XAxis.XAxisPosition.BOTTOM
                setDrawGridLines(true)
                valueFormatter = object : ValueFormatter() {
                    override fun getFormattedValue(value: Float): String = String.format(Locale.getDefault(), "%.2fs", value)
                }
            }
            axisLeft.apply { textColor = Color.RED; axisMinimum = 0f; setDrawGridLines(true) }
            axisRight.apply { isEnabled = true; textColor = Color.BLUE; axisMinimum = 0f; setDrawGridLines(false) }
            legend.isEnabled = true
        }
    }

    private fun setupFlightChart() {
        flightChart.apply {
            description.text = "Analiza rotacji w locie"; description.textColor = Color.BLACK
            setTouchEnabled(true); setPinchZoom(true); setBackgroundColor(Color.parseColor("#F0F8FF"))
            xAxis.apply {
                position = com.github.mikephil.charting.components.XAxis.XAxisPosition.BOTTOM
                setDrawGridLines(true)
                valueFormatter = object : ValueFormatter() {
                    override fun getFormattedValue(value: Float): String = String.format(Locale.getDefault(), "%.1fs", value)
                }
            }
            axisLeft.apply { textColor = Color.BLUE; axisMinimum = 0f }
            axisRight.isEnabled = false; legend.isEnabled = true
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        try {
            clientSocket?.close() // Zmiana z serverSocket na clientSocket
            logWriter?.close()
        } catch (e: Exception) {
            Log.e(LOG_TAG, "Błąd zamykania zasobów: ${e.message}")
        }
        tcpServerJob?.cancel()
    }
}