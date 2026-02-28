package com.example.odbior_pomiarow

import android.content.Context
import android.content.Intent
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
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import kotlinx.coroutines.*
import java.io.File
import java.io.FileWriter
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.*
import kotlin.math.sqrt
import com.github.mikephil.charting.formatter.ValueFormatter

class MainActivity : AppCompatActivity() {


    companion object {
        // Lista statyczna dostępna dla HistoryActivity - przechowuje całą historię sesji
        val fullHitHistory = mutableListOf<HistoryEntry>()
    }

    // Stałe konfiguracyjne
    private val UDP_PORT = 5000               // Port nasłuchu UDP
    private val UDP_HOST = "0.0.0.0"          // Nasłuch na wszystkich interfejsach
    private val LOG_TAG = "UDP_LOGGER"        // Tag do logów w konsoli Android Studio
    private val LOG_FILE_PREFIX = "pomiary"   // Prefiks nazwy pliku CSV
    private val UI_UPDATE_INTERVAL_MS = 50L   // Ograniczenie odświeżania UI (20 FPS)
    private val HEARTBEAT_TIMEOUT_MS = 5000L  // Czas po którym uznajemy utratę połączenia

    // --- ELEMENTY UI (WIDOKI) ---
    private lateinit var collisionChart: LineChart     // Wykres uderzeń
    private lateinit var flightChart: LineChart        // Wykres lotu
    private lateinit var lastHitTextView: TextView     // Tekst ostatniego zdarzenia
    private lateinit var statusTextView: TextView      // Status operacji (np. "Odbieram dane")
    private lateinit var connectionStatusTextView: TextView // Status połączenia (Online/Offline)

    // Pola tekstowe z wartościami sensorów na żywo
    private lateinit var accelX: TextView; private lateinit var accelY: TextView; private lateinit var accelZ: TextView
    private lateinit var gyroX: TextView; private lateinit var gyroY: TextView; private lateinit var gyroZ: TextView

    private lateinit var hitHistoryContainer: LinearLayout // Kontener na miniaturową listę historii
    private lateinit var logSwitch: Switch                 // Przełącznik zapisu do pliku

    // --- ZMIENNE LOGIKI APLIKACJI ---

    // Bufory danych
    private val eventSamples = mutableListOf<SampleData>()          // Bufor na próbki uderzenia
    private val currentIntervalSamples = mutableListOf<SampleData>() // Bufor na próbki lotu (między uderzeniami)

    // Stan maszyny stanów
    private var receiveState = ReceiveState.NORMAL     // Aktualny stan odbioru
    private var expectedEventSize = 0                  // Oczekiwana liczba próbek w zdarzeniu
    private var eventDurationMs = 0L                   // Czas trwania uderzenia (z nagłówka)
    private var eventStartTimeMs = 0L                  // Czas startu (systemowy)

    // Zmienne pomocnicze
    private var lastUiUpdateTime = 0L                  // Do limitowania FPS interfejsu
    private var lastPacketTime = 0L                    // Czas ostatniego pakietu (do heartbeat)

    // ZMIENNE SIECIOWE I PLIKOWE
    private var multicastLock: WifiManager.MulticastLock? = null // Blokada, aby system nie ubijał UDP
    private val scope = CoroutineScope(Dispatchers.IO)           // Scope dla wątków tła (sieć/dysk)
    private var receiveJob: Job? = null                          // Uchwyt do wątku nasłuchu UDP

    // Logowanie do pliku
    private var logWriter: FileWriter? = null          // Uchwyt do pliku
    private var isLoggingEnabled = false               // Flaga czy zapisywać
    private var logFile: File? = null                  // Obiekt pliku
    private var logHeaderWritten = false               // Czy nagłówek CSV został już zapisany

    // Dynamiczna nazwa pliku (generowana przy dostępie)
    private val logFileName: String
        get() = "${LOG_FILE_PREFIX}_${SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())}.csv"

    // CYKL ŻYCIA AKTYWNOŚCI

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge() // Włączenie trybu pełnoekranowego
        setContentView(R.layout.activity_main)

        // Obsługa marginesów systemowych
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main)) { v, insets ->
            val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
            insets
        }

        initializeViews()     // Przypisanie widoków
        setupChart()          // Konfiguracja wykresu uderzeń
        setupFlightChart()    // Konfiguracja wykresu lotu

        startUdpListener()            // Start wątku sieciowego
        startConnectionStatusChecker() // Start monitora połączenia
    }

    //Przypisuje elementy z pliku XML do zmiennych w kodzie i ustawia listenery przycisków.
    private fun initializeViews() {
        // Przypisanie elementów z layoutu XML do zmiennych
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

        accelX = findViewById(R.id.accelX); accelY = findViewById(R.id.accelY); accelZ = findViewById(R.id.accelZ)
        gyroX = findViewById(R.id.gyroX); gyroY = findViewById(R.id.gyroY); gyroZ = findViewById(R.id.gyroZ)

        hitHistoryContainer = findViewById(R.id.hitHistoryContainer)
        logSwitch = findViewById(R.id.logSwitch)

        // Obsługa przełącznika logowania do pliku
        logSwitch.setOnCheckedChangeListener { _, isChecked ->
            if (isChecked) initLogFile() // Włącz
            else {                       // Wyłącz
                isLoggingEnabled = false
                logWriter?.close()
                logWriter = null
                Log.i(LOG_TAG, "Logowanie wyłączone.")
            }
        }
    }

    // --- OBSŁUGA SIECI (UDP) ---

    //uruchamia wątek w tle (Coroutine), który nasłuchuje pakietów UDP na porcie 5000.
    private fun startUdpListener() {
        // Pobranie menedżera Wi-Fi i założenie blokady Multicast
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        multicastLock = wifiManager.createMulticastLock("udp_lock")
        multicastLock?.setReferenceCounted(true)
        multicastLock?.acquire()

        receiveJob = scope.launch {
            try {
                // Utworzenie gniazda UDP
                val socket = DatagramSocket(UDP_PORT, InetAddress.getByName(UDP_HOST))
                socket.broadcast = true // Zezwolenie na pakiety rozgłoszeniowe

                val buffer = ByteArray(2048) // Bufor odbiorczy
                val packet = DatagramPacket(buffer, buffer.size)

                while (isActive) {
                    socket.receive(packet) // Blokuje wątek do momentu nadejścia pakietu
                    updateHeartbeat()      // Zaktualizuj status "Połączono"

                    // Kopia faktycznych danych (bez pustych bajtów na końcu bufora)
                    val rawData = packet.data.copyOfRange(0, packet.length)
                    processBinaryData(rawData) // Parsowanie danych
                }
            } catch (e: Exception) {
                Log.e(LOG_TAG, "Błąd UDP: ${e.message}")
            }
        }
    }

    //Główny parser danych. Rozpoznaje nagłówki (SMPL, DEADBEEF) i kieruje dane do odpowiedniej funkcji.
    private fun processBinaryData(data: ByteArray) {
        if (data.size < 4) return

        // Opakowanie bajtów w ByteBuffer dla łatwiejszego odczytu (Little Endian jak w ESP32)
        val buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        val firstInt = buffer.getInt(0)
        // Sprawdzenie czy pakiet zaczyna się od napisu "SMPL"
        val firstFourChars = if (data.size >= 4) String(data.take(4).toByteArray()) else ""

        when {
            //LIVE STREAM: Nagłówek "SMPL"
            firstFourChars == "SMPL" -> {
                if (data.size >= 32) decodeSample(buffer, 4, isLive = true)
            }
            //START ZDARZENIA: Nagłówek 0xDEADBEEF
            firstInt == 0xDEADBEEF.toInt() -> {
                handleBinaryEventStart(buffer)
            }
            //KONIEC ZDARZENIA: Nagłówek 0xEEEEEEEE
            firstInt == 0xEEEEEEEE.toInt() -> {
                handleEventEnd()
            }
            //DANE ZDARZENIA: Brak nagłówka, ale jesteśmy w trybie EVENT_ACTIVE
            receiveState == ReceiveState.EVENT_ACTIVE -> {
                // Oblicz ile próbek mieści się w pakiecie
                val numSamples = data.size / 28
                for (i in 0 until numSamples) {
                    decodeSample(buffer, i * 28, isLive = false)
                }
                // Zabezpieczenie: jeśli mamy już komplet danych, zakończ
                if (eventSamples.size >= expectedEventSize) {
                    handleEventEnd()
                }
            }
        }
    }

    // Dekodowanie pojedynczej próbki z bajtów na liczby
    private fun decodeSample(buffer: ByteBuffer, offset: Int, isLive: Boolean) {
        buffer.position(offset)
        // Odczyt floatów i konwersja jednostek
        val ax = buffer.getFloat() * 1000f // g -> mg
        val ay = buffer.getFloat() * 1000f
        val az = buffer.getFloat() * 1000f
        // Radiany na stopnie (rad -> deg)
        val radToDeg = (180f / Math.PI.toFloat())
        val gx = buffer.getFloat() * radToDeg
        val gy = buffer.getFloat() * radToDeg
        val gz = buffer.getFloat() * radToDeg

        // Timestamp (rzutowanie na Long unsigned)
        val ts = buffer.getInt().toLong() and 0xFFFFFFFFL

        val currentSample = SampleData(ax, ay, az, gx, gy, gz, ts)

        if (isLive) {
            // Aktualizacja UI na żywo
            displaySensorData(ax, ay, az, gx, gy, gz)
            // Analiza lotu tylko gdy NIE trwa uderzenie
            if (receiveState == ReceiveState.NORMAL) {
                analyzeFlight(currentSample)
            }
        } else {
            // Zbieranie danych do analizy uderzenia po fakcie
            if (receiveState == ReceiveState.EVENT_ACTIVE && eventSamples.isEmpty()) {
                eventStartTimeMs = System.currentTimeMillis() // Ustalenie czasu początkowego
            }
            eventSamples.add(currentSample)
        }
    }

    //LOGIKA ZDARZEŃ (LOT I UDERZENIE)

    //Inicjuje zbieranie danych uderzenia.
    private fun handleBinaryEventStart(buffer: ByteBuffer) {
        if (receiveState != ReceiveState.NORMAL) return

        // Jeśli zgromadziliśmy dane lotu przed uderzeniem -> przetwórz je teraz
        if (currentIntervalSamples.size > 10) {
            processFlightData()
        }
        currentIntervalSamples.clear() // Czyść bufor lotu

        // Odczyt parametrów z nagłówka START
        buffer.position(4)
        expectedEventSize = buffer.getInt()    // Ile próbek
        eventDurationMs = buffer.getInt().toLong() // Czas trwania wg ESP
        eventStartTimeMs = 0L
        eventSamples.clear()

        // Zmiana stanu na odbiór zdarzenia
        receiveState = ReceiveState.EVENT_ACTIVE

        runOnUiThread {
            statusTextView.text = "!!! ZDARZENIE UDERZENIA WYKRYTE !!!"
            statusTextView.setTextColor(Color.RED)
        }
    }

    //Liczy statystyki i aktualizuje UI.
    private fun handleEventEnd() {
        val samplesToDraw = ArrayList(eventSamples) // Kopia danych
        receiveState = ReceiveState.NORMAL          // Powrót do nasłuchu

        if (samplesToDraw.size < 5) {
            eventSamples.clear()
            return
        }

        // Obliczenia statystyk uderzenia
        val startTimeUs = samplesToDraw.first().timestamp
        val endTimeUs = samplesToDraw.last().timestamp
        val realDurationMs = (endTimeUs - startTimeUs) / 1000L
        // Obliczenie max siły wypadkowej
        val peakForce = samplesToDraw.maxOf {
            sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()).toFloat()
        }

        runOnUiThread {
            addHitToHistory(Date(eventStartTimeMs), realDurationMs, peakForce, samplesToDraw)
            drawCollisionChart(samplesToDraw)
            statusTextView.text = "Zderzenie przetworzone"
        }
        eventSamples.clear()
    }

    //Zbiera próbki do bufora "lotu" w czasie rzeczywistym, gdy nie ma zderzenia
    private fun analyzeFlight(sample: SampleData) {
        // Dodaj próbkę do bufora lotu (zbieranie danych "w tle")
        currentIntervalSamples.add(sample)
    }

    //Przetwarza zebrane dane lotu (przed zderzeniem), oblicza rotację i zapisuje w historii
    private fun processFlightData() {
        if (currentIntervalSamples.isEmpty()) return

        val flightSegment = ArrayList(currentIntervalSamples)
        val startTime = flightSegment.first().timestamp
        val duration = (flightSegment.last().timestamp - startTime) / 1000L

        // Oblicz średnią rotację
        val avgRotation = flightSegment.map {
            sqrt((it.gx * it.gx + it.gy * it.gy + it.gz * it.gz).toDouble()).toFloat()
        }.average().toFloat()

        // Dodaj wpis LOTU do historii
        val newEntry = HistoryEntry(EntryType.FLIGHT, Date(), duration, flightSegment, peakValue = avgRotation)
        MainActivity.fullHitHistory.add(0, newEntry)

        runOnUiThread {
            drawFlightChart(flightSegment)
            lastHitTextView.text = String.format("Ostatni lot: %d ms | Obrót: %.1f dps", duration, avgRotation)
            lastHitTextView.setBackgroundColor(Color.CYAN)
        }
    }

    //RYSOWANIE WYKRESÓW I UI

    //Rysowanie wykresow uderzenia
    private fun drawCollisionChart(samples: List<SampleData>) {
        // Przygotowanie list punktów (Entry) dla biblioteki MPAndroidChart
        val accelEntries = mutableListOf<Entry>()
        val gyroEntries = mutableListOf<Entry>()
        val startTs = samples.first().timestamp

        samples.forEach { s ->
            val x = (s.timestamp - startTs) / 1000000f // Czas w sekundach
            // Siła wypadkowa akcelerometru
            val aMag = sqrt((s.ax * s.ax + s.ay * s.ay + s.az * s.az).toDouble()).toFloat() / 1000f
            accelEntries.add(Entry(x, aMag))

            // Siła wypadkowa żyroskopu
            val gMag = sqrt((s.gx * s.gx + s.gy * s.gy + s.gz * s.gz).toDouble()).toFloat()
            gyroEntries.add(Entry(x, gMag))
        }

        // Konfiguracja linii wykresu
        val setA = LineDataSet(accelEntries, "Siła [G]").apply {
            color = Color.RED; axisDependency = com.github.mikephil.charting.components.YAxis.AxisDependency.LEFT
            setDrawCircles(false); lineWidth = 2.5f; setDrawValues(false)
        }
        val setG = LineDataSet(gyroEntries, "Rotacja [dps]").apply {
            color = Color.BLUE; axisDependency = com.github.mikephil.charting.components.YAxis.AxisDependency.RIGHT
            setDrawCircles(false); lineWidth = 2.5f; setDrawValues(false)
        }

        collisionChart.data = LineData(setA, setG)
        collisionChart.invalidate() // Odświeżenie widoku
    }

    // Funkcja dodająca wpis do historii (UI) i listy
    private fun addHitToHistory(hitDate: Date, durationMs: Long, peakForce: Float, currentSamples: List<SampleData>) {

        val newHit = HistoryEntry(EntryType.HIT, hitDate, durationMs, currentSamples, peakValue = peakForce)
        fullHitHistory.add(0, newHit) // Dodanie na początek listy

        runOnUiThread {
            val forceInG = peakForce / 1000f
            lastHitTextView.text = String.format("Ostatnie: %.2f G (%d ms)", forceInG, durationMs)
            lastHitTextView.setBackgroundColor(Color.parseColor("#FFD700"))

            // Dodanie małego elementu tekstowego do listy na ekranie głównym
            val quickSummary = TextView(this).apply {
                text = String.format("💥 %.2f G | %d ms", forceInG, durationMs)
                textSize = 14f
            }
            hitHistoryContainer.addView(quickSummary, 0)
        }
    }

    //Aktualizuje tekstowe pola wartości sensorów
    private fun displaySensorData(ax: Float?, ay: Float?, az: Float?, gx: Float?, gy: Float?, gz: Float?) {
        val now = System.currentTimeMillis()
        // Limitowanie odświeżania UI dla wydajności
        if (now - lastUiUpdateTime < UI_UPDATE_INTERVAL_MS) return
        lastUiUpdateTime = now

        runOnUiThread {
            ax?.let { accelX.text = String.format("AX: %.2f mg", it) }
            ay?.let { accelY.text = String.format("AY: %.2f mg", it) }
            az?.let { accelZ.text = String.format("AZ: %.2f mg", it) }
            gx?.let { gyroX.text = String.format("GX: %.2f dps", it) }
            gy?.let { gyroY.text = String.format("GY: %.2f dps", it) }
            gz?.let { gyroZ.text = String.format("GZ: %.2f dps", it) }
            statusTextView.text = "Status: Odbieram dane..."
            statusTextView.setTextColor(Color.BLACK)
        }
    }

    //OBSŁUGA POŁĄCZENIA

    //Odświeża czas ostatniego pakietu i ustawia status na "Połączono".
    private fun updateHeartbeat() {
        lastPacketTime = System.currentTimeMillis()
        runOnUiThread {
            connectionStatusTextView.text = "Status: Połączono"
            connectionStatusTextView.setTextColor(Color.BLACK)
            connectionStatusTextView.setBackgroundColor(Color.parseColor("#B3FFB3"))
        }
    }

    //Wątek monitorujący aktywność sieci. Jeśli brak pakietów > 5s, ustawia status "Rozłączono".
    private fun startConnectionStatusChecker() {
        scope.launch(Dispatchers.Main) {
            while (isActive) {
                delay(1000L) // Sprawdzanie co sekundę
                if (System.currentTimeMillis() - lastPacketTime > HEARTBEAT_TIMEOUT_MS) {
                    connectionStatusTextView.text = "Status: Rozłączono"
                    connectionStatusTextView.setTextColor(Color.RED)
                    connectionStatusTextView.setBackgroundColor(Color.parseColor("#FFB3B3"))
                }
            }
        }
    }

    // OBSŁUGA PLIKU
    private fun initLogFile() {
        val appDirectory = getExternalFilesDir(null)
        if (appDirectory == null) return

        logFile = File(appDirectory, logFileName)
        try {
            logWriter = FileWriter(logFile, true)
            isLoggingEnabled = true
            Toast.makeText(this, "Zapis do: ${logFile?.name}", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            isLoggingEnabled = false
            logSwitch.isChecked = false
        }
    }

    // SETUP WYKRESÓW
    // 1. Konfiguracja wykresu ZDERZEŃ (Collision Chart)
    private fun setupChart() {
        collisionChart.apply {
            description.isEnabled = false
            setTouchEnabled(true)
            isDragEnabled = true
            setScaleEnabled(true)
            setPinchZoom(true)
            setBackgroundColor(Color.WHITE)

            // Konfiguracja osi X (Czas)
            xAxis.apply {
                position = com.github.mikephil.charting.components.XAxis.XAxisPosition.BOTTOM
                setDrawGridLines(true)
                // Formatowanie czasu: np. "0.50s"
                valueFormatter = object : ValueFormatter() {
                    override fun getFormattedValue(value: Float): String {
                        return String.format(Locale.getDefault(), "%.2fs", value)
                    }
                }
            }

            // Oś Y lewa (Czerwona) -> Akcelerometr (Siła G)
            axisLeft.apply {
                textColor = Color.RED
                axisMinimum = 0f
                setDrawGridLines(true)
            }

            // Oś Y prawa (Niebieska) -> Żyroskop (Rotacja dps)
            axisRight.apply {
                isEnabled = true
                textColor = Color.BLUE
                axisMinimum = 0f
                setDrawGridLines(false)
            }

            legend.isEnabled = true
        }
    }

    //Konfiguracja wykresu LOTU (Flight Chart)
    private fun setupFlightChart() {
        flightChart.apply {
            description.text = "Analiza rotacji w locie"
            description.textColor = Color.BLACK
            setTouchEnabled(true)
            setPinchZoom(true)
            setBackgroundColor(Color.parseColor("#F0F8FF")) // Lekko niebieskie tło dla lotu

            xAxis.apply {
                position = com.github.mikephil.charting.components.XAxis.XAxisPosition.BOTTOM
                setDrawGridLines(true)
                valueFormatter = object : ValueFormatter() {
                    override fun getFormattedValue(value: Float): String {
                        return String.format(Locale.getDefault(), "%.1fs", value)
                    }
                }
            }


            axisLeft.apply {
                textColor = Color.BLUE
                axisMinimum = 0f
            }

            axisRight.isEnabled = false
            legend.isEnabled = true
        }
    }

    //Rysowanie danych LOTU (Tylko żyroskop)
    private fun drawFlightChart(samples: List<SampleData>) {
        if (samples.isEmpty()) return

        val gyroEntries = ArrayList<Entry>()
        val startTs = samples.first().timestamp

        // Przetwarzanie próbek
        samples.forEach { s ->

            val timeSec = (s.timestamp - startTs) / 1000000f


            val gMag = sqrt((s.gx * s.gx + s.gy * s.gy + s.gz * s.gz).toDouble()).toFloat()

            gyroEntries.add(Entry(timeSec, gMag))
        }


        val setG = LineDataSet(gyroEntries, "Rotacja [dps]").apply {
            color = Color.BLUE
            lineWidth = 2f
            setDrawCircles(false)
            setDrawValues(false)
            mode = LineDataSet.Mode.CUBIC_BEZIER
            setDrawFilled(true)
            fillColor = Color.BLUE
            fillAlpha = 50
        }

        // Przypisanie danych do wykresu i odświeżenie
        flightChart.data = LineData(setG)
        flightChart.invalidate()
        flightChart.animateX(500)
    }
    override fun onDestroy() {
        super.onDestroy()
        multicastLock?.release() // Zwolnienie blokady Wi-Fi
        receiveJob?.cancel()     // Zatrzymanie wątku
    }
}