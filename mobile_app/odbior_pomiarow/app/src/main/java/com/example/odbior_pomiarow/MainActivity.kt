package com.example.odbior_pomiarow

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.MotionEvent
import android.widget.Button
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.YAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import com.github.mikephil.charting.listener.ChartTouchListener
import com.github.mikephil.charting.listener.OnChartGestureListener
import kotlinx.coroutines.*
import java.io.File
import java.io.FileWriter
import java.text.SimpleDateFormat
import java.util.*
import kotlin.math.sqrt

class MainActivity : AppCompatActivity() {

    companion object {
        val fullHitHistory = mutableListOf<HistoryEntry>()
    }

    private val BLE_PERMISSION_REQUEST_CODE = 101
    private val LOG_TAG = "MAIN_ACTIVITY"
    private val LOG_FILE_PREFIX = "pomiary"

    // --- DWA WIDOKI WYKRESÓW ---
    private lateinit var accelChart: LineChart
    private lateinit var gyroChart: LineChart
    private var chartSampleCount = 0f
    private val MAX_VISIBLE_SAMPLES = 200

    // Bufor próbek dla aktualnie rejestrowanego zdarzenia
    private val currentHitSamples = mutableListOf<SampleData>()

    // Flag zabezpieczający przed nieskończoną pętlą synchronizacji gestów
    private var isSyncingCharts = false

    // --- ELEMENTY UI ---
    private lateinit var lastHitTextView: TextView
    private lateinit var statusTextView: TextView
    private lateinit var connectionStatusTextView: TextView
    private lateinit var btnOpenLastShot: Button

    private lateinit var tvSessionShots: TextView
    private lateinit var tvSessionEnergy: TextView
    private lateinit var tvSessionConsistency: TextView

    private lateinit var h3AccelX: TextView; private lateinit var h3AccelY: TextView; private lateinit var h3AccelZ: TextView
    private lateinit var imuAccelX: TextView; private lateinit var imuAccelY: TextView; private lateinit var imuAccelZ: TextView
    private lateinit var gyroX: TextView; private lateinit var gyroY: TextView; private lateinit var gyroZ: TextView
    private lateinit var gpsLat: TextView; private lateinit var gpsLon: TextView; private lateinit var gpsFix: TextView

    private lateinit var hitHistoryContainer: LinearLayout
    private lateinit var logSwitch: Switch

    private val scope = CoroutineScope(Dispatchers.IO)
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

        BleManager.onGpsDataReceivedListener = { lat, lon, fix ->
            runOnUiThread {
                gpsLat.text = String.format("Lat: %.5f", lat)
                gpsLon.text = String.format("Lon: %.5f", lon)
                gpsFix.text = if (fix) "FIX: TAK" else "FIX: NIE"
            }
        }

        BleManager.onDataSampleReceivedListener = { packetType, h3x, h3y, h3z, ax, ay, az, gx, gy, gz ->
            runOnUiThread {
                // 1. Dodanie nowej próbki na żywo do wykresów
                addSampleToChart(h3x, h3y, h3z, ax, ay, az, gx, gy, gz)

                // 2. Aktualizacja kontenerów tekstowych H3 High-G
                h3AccelX.text = String.format("X: %.2f G", h3x)
                h3AccelY.text = String.format("Y: %.2f G", h3y)
                h3AccelZ.text = String.format("Z: %.2f G", h3z)

                // 3. Aktualizacja kontenerów tekstowych IMU Accel
                imuAccelX.text = String.format("X: %.2f mg", ax)
                imuAccelY.text = String.format("Y: %.2f mg", ay)
                imuAccelZ.text = String.format("Z: %.2f mg", az)

                // 4. Aktualizacja kontenerów tekstowych Żyroskopu
                gyroX.text = String.format("X: %.1f dps", gx)
                gyroY.text = String.format("Y: %.1f dps", gy)
                gyroZ.text = String.format("Z: %.1f dps", gz)

                // 5. OBSŁUGA DETEKCJI ZDARZENIA (packetType == 1)
                if (packetType == 1.toByte()) {
                    val sample = SampleData(
                        h3x = h3x, h3y = h3y, h3z = h3z,
                        ax = ax, ay = ay, az = az,
                        gx = gx, gy = gy, gz = gz,
                        timestamp = System.currentTimeMillis()
                    )
                    currentHitSamples.add(sample)
                } else if (currentHitSamples.isNotEmpty()) {
                    // Wyliczenie wartości szczytowej w jednostkach G z akcelerometru H3 High-G
                    val peakG = currentHitSamples.maxOf { s ->
                        sqrt((s.h3x * s.h3x + s.h3y * s.h3y + s.h3z * s.h3z).toDouble()).toFloat()
                    }

                    val newEntry = HistoryEntry(
                        type = EntryType.HIT,
                        date = Date(),
                        durationMs = currentHitSamples.size * 10L,
                        samples = ArrayList(currentHitSamples),
                        peakValue = peakG
                    )

                    fullHitHistory.add(0, newEntry)

                    // Natychmiastowa aktualizacja etykiety UI
                    lastHitTextView.text = String.format("Ostatnie uderzenie: %.2f G", peakG)
                    Toast.makeText(this@MainActivity, "Wykryto nowe uderzenie: ${String.format("%.2f", peakG)} G!", Toast.LENGTH_SHORT).show()

                    currentHitSamples.clear()
                }
            }
        }

        initializeViews()

        BleManager.init(this)
        BleManager.onConnectionStateChanged = { isConnected ->
            runOnUiThread {
                if (isConnected) {
                    connectionStatusTextView.text = "Status: Połączono BLE"
                    connectionStatusTextView.setBackgroundColor(Color.parseColor("#B3FFB3"))
                } else {
                    connectionStatusTextView.text = "Status: Rozłączono BLE"
                    connectionStatusTextView.setBackgroundColor(Color.parseColor("#FFB3B3"))
                }
            }
        }

        checkAndRequestBlePermissions()
    }

    private fun setupDualCharts() {
        accelChart.apply {
            description.text = "Przyspieszenie (Lewa: IMU [mg], Prawa: H3 High-G [G])"
            setTouchEnabled(true)
            isDragEnabled = true
            isScaleXEnabled = true
            isScaleYEnabled = false
            setPinchZoom(false)
            setBackgroundColor(Color.WHITE)
        }

        val setImuAccel = LineDataSet(mutableListOf(), "IMU Accel [mg]").apply {
            color = Color.RED
            setDrawCircles(false)
            lineWidth = 2f
            setDrawValues(false)
            axisDependency = YAxis.AxisDependency.LEFT
        }

        val setH3Accel = LineDataSet(mutableListOf(), "H3 High-G [G]").apply {
            color = Color.BLACK
            setDrawCircles(false)
            lineWidth = 2f
            setDrawValues(false)
            axisDependency = YAxis.AxisDependency.RIGHT
        }

        accelChart.data = LineData(setImuAccel, setH3Accel)

        accelChart.axisLeft.apply {
            textColor = Color.RED
            setDrawGridLines(true)
            resetAxisMinimum()
            resetAxisMaximum()
        }

        accelChart.axisRight.apply {
            isEnabled = true
            textColor = Color.BLACK
            setDrawGridLines(false)
            resetAxisMinimum()
            resetAxisMaximum()
        }

        gyroChart.apply {
            description.text = "Rotacja [dps]"
            setTouchEnabled(true)
            isDragEnabled = true
            isScaleXEnabled = true
            isScaleYEnabled = false
            setPinchZoom(false)
            setBackgroundColor(Color.WHITE)
        }

        val setGyroX = LineDataSet(mutableListOf(), "Żyroskop X").apply {
            color = Color.BLUE
            setDrawCircles(false)
            lineWidth = 2.5f
            setDrawValues(false)
            axisDependency = YAxis.AxisDependency.LEFT
        }

        val setGyroY = LineDataSet(mutableListOf(), "Żyroskop Y").apply {
            color = Color.parseColor("#4CAF50")
            setDrawCircles(false)
            lineWidth = 2.5f
            setDrawValues(false)
            axisDependency = YAxis.AxisDependency.LEFT
        }

        val setGyroZ = LineDataSet(mutableListOf(), "Żyroskop Z").apply {
            color = Color.parseColor("#FF9800")
            setDrawCircles(false)
            lineWidth = 2.5f
            setDrawValues(false)
            axisDependency = YAxis.AxisDependency.LEFT
        }

        gyroChart.data = LineData(setGyroX, setGyroY, setGyroZ)

        gyroChart.axisLeft.apply {
            textColor = Color.BLACK
            setDrawGridLines(true)
            resetAxisMinimum()
            resetAxisMaximum()
            spaceTop = 20f
            spaceBottom = 20f
        }
        gyroChart.axisRight.isEnabled = false

        accelChart.onChartGestureListener = createSyncGestureListener(accelChart, gyroChart)
        gyroChart.onChartGestureListener = createSyncGestureListener(gyroChart, accelChart)
    }

    private fun createSyncGestureListener(srcChart: LineChart, dstChart: LineChart): OnChartGestureListener {
        return object : OnChartGestureListener {
            override fun onChartScale(me: MotionEvent?, scaleX: Float, scaleY: Float) {
                syncCharts(srcChart, dstChart)
            }

            override fun onChartTranslate(me: MotionEvent?, dX: Float, dY: Float) {
                syncCharts(srcChart, dstChart)
            }

            override fun onChartGestureStart(me: MotionEvent?, lastPerformedGesture: ChartTouchListener.ChartGesture?) {}
            override fun onChartGestureEnd(me: MotionEvent?, lastPerformedGesture: ChartTouchListener.ChartGesture?) {}
            override fun onChartLongPressed(me: MotionEvent?) {}
            override fun onChartDoubleTapped(me: MotionEvent?) {}
            override fun onChartSingleTapped(me: MotionEvent?) {}
            override fun onChartFling(me1: MotionEvent?, me2: MotionEvent?, velocityX: Float, velocityY: Float) {}
        }
    }

    private fun syncCharts(src: LineChart, dst: LineChart) {
        if (isSyncingCharts) return
        isSyncingCharts = true

        val srcMatrix = src.viewPortHandler.matrixTouch
        val dstMatrix = dst.viewPortHandler.matrixTouch

        val vals = FloatArray(9)
        srcMatrix.getValues(vals)

        val dstVals = FloatArray(9)
        dstMatrix.getValues(dstVals)
        dstVals[0] = vals[0]
        dstVals[2] = vals[2]

        dstMatrix.setValues(dstVals)
        dst.viewPortHandler.refresh(dstMatrix, dst, true)

        isSyncingCharts = false
    }

    private fun addSampleToChart(h3x: Float, h3y: Float, h3z: Float, ax: Float, ay: Float, az: Float, gx: Float, gy: Float, gz: Float) {
        scope.launch(Dispatchers.IO) {
            SessionManager.logSampleToCurrentSession(
                h3x, h3y, h3z,
                ax, ay, az,
                gx, gy, gz,
                0f, 0f, false
            )
        }

        runOnUiThread {
            val accelData = accelChart.data ?: return@runOnUiThread
            val gyroData = gyroChart.data ?: return@runOnUiThread

            val setImu = accelData.getDataSetByIndex(0)
            val setH3 = accelData.getDataSetByIndex(1)

            val setGyroX = gyroData.getDataSetByIndex(0)
            val setGyroY = gyroData.getDataSetByIndex(1)
            val setGyroZ = gyroData.getDataSetByIndex(2)

            val imuMagMg = sqrt((ax * ax + ay * ay + az * az).toDouble()).toFloat()
            val h3MagG = sqrt((h3x * h3x + h3y * h3y + h3z * h3z).toDouble()).toFloat()

            accelData.addEntry(Entry(chartSampleCount, imuMagMg), 0)
            accelData.addEntry(Entry(chartSampleCount, h3MagG), 1)

            gyroData.addEntry(Entry(chartSampleCount, gx), 0)
            gyroData.addEntry(Entry(chartSampleCount, gy), 1)
            gyroData.addEntry(Entry(chartSampleCount, gz), 2)

            chartSampleCount++

            if (setImu.entryCount > MAX_VISIBLE_SAMPLES) setImu.removeEntry(0)
            if (setH3.entryCount > MAX_VISIBLE_SAMPLES) setH3.removeEntry(0)

            if (setGyroX.entryCount > MAX_VISIBLE_SAMPLES) setGyroX.removeEntry(0)
            if (setGyroY.entryCount > MAX_VISIBLE_SAMPLES) setGyroY.removeEntry(0)
            if (setGyroZ.entryCount > MAX_VISIBLE_SAMPLES) setGyroZ.removeEntry(0)

            accelData.notifyDataChanged()
            gyroData.notifyDataChanged()
            accelChart.notifyDataSetChanged()
            gyroChart.notifyDataSetChanged()

            val minX = (chartSampleCount - MAX_VISIBLE_SAMPLES).coerceAtLeast(0f)
            accelChart.xAxis.axisMinimum = minX
            accelChart.xAxis.axisMaximum = chartSampleCount

            gyroChart.xAxis.axisMinimum = minX
            gyroChart.xAxis.axisMaximum = chartSampleCount

            accelChart.invalidate()
            gyroChart.invalidate()
        }
    }

    private fun initializeViews() {
        findViewById<Button>(R.id.btnSettings).setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }
        findViewById<Button>(R.id.btnShowHistory).setOnClickListener {
            startActivity(Intent(this, HistoryActivity::class.java))
        }
        findViewById<Button>(R.id.btnShowLocation).setOnClickListener {
            startActivity(Intent(this, LocationActivity::class.java))
        }
        findViewById<Button>(R.id.btnOpenSessions)?.setOnClickListener {
            startActivity(Intent(this, SessionActivity::class.java))
        }

        accelChart = findViewById(R.id.accelLiveChart)
        gyroChart = findViewById(R.id.gyroLiveChart)
        setupDualCharts()

        lastHitTextView = findViewById(R.id.lastHitTextView)
        statusTextView = findViewById(R.id.statusTextView)
        connectionStatusTextView = findViewById(R.id.connectionStatusTextView)

        btnOpenLastShot = findViewById(R.id.btnOpenLastShot)
        btnOpenLastShot.setOnClickListener {
            if (fullHitHistory.isNotEmpty()) {
                val lastEntry = fullHitHistory[0]
                ShotDetailsActivity.selectedSamples = lastEntry.samples
                ShotDetailsActivity.peakValue = lastEntry.peakValue
                ShotDetailsActivity.entryType = lastEntry.type

                val intent = Intent(this, ShotDetailsActivity::class.java)
                startActivity(intent)
            } else {
                Toast.makeText(this, "Brak zarejestrowanych zdarzeń w obecnej sesji!", Toast.LENGTH_SHORT).show()
            }
        }

        tvSessionShots = findViewById(R.id.tvSessionShots)
        tvSessionEnergy = findViewById(R.id.tvSessionEnergy)
        tvSessionConsistency = findViewById(R.id.tvSessionConsistency)

        h3AccelX = findViewById(R.id.h3AccelX)
        h3AccelY = findViewById(R.id.h3AccelY)
        h3AccelZ = findViewById(R.id.h3AccelZ)

        imuAccelX = findViewById(R.id.imuAccelX)
        imuAccelY = findViewById(R.id.imuAccelY)
        imuAccelZ = findViewById(R.id.imuAccelZ)

        gyroX = findViewById(R.id.gyroX)
        gyroY = findViewById(R.id.gyroY)
        gyroZ = findViewById(R.id.gyroZ)

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

    private fun checkAndRequestBlePermissions() {
        val permissionsToRequest = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {
                permissionsToRequest.add(Manifest.permission.BLUETOOTH_SCAN)
            }
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
                permissionsToRequest.add(Manifest.permission.BLUETOOTH_CONNECT)
            }
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
                permissionsToRequest.add(Manifest.permission.ACCESS_FINE_LOCATION)
            }
        }

        if (permissionsToRequest.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, permissionsToRequest.toTypedArray(), BLE_PERMISSION_REQUEST_CODE)
        } else {
            BleManager.startScanAndConnect(this)
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == BLE_PERMISSION_REQUEST_CODE) {
            if (grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
                BleManager.startScanAndConnect(this)
            } else {
                Toast.makeText(this, "Aplikacja wymaga uprawnień Bluetooth!", Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun initLogFile() {
        val appDirectory = getExternalFilesDir(null) ?: return
        logFile = File(appDirectory, logFileName)
        try {
            logWriter = FileWriter(logFile, true)
            logWriter?.write("Timestamp,H3_AX,H3_AY,H3_AZ,IMU_AX,IMU_AY,IMU_AZ,IMU_GX,IMU_GY,IMU_GZ,Lat,Lon,Fix\n")
            isLoggingEnabled = true
            Toast.makeText(this, "Zapis do: ${logFile?.name}", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            isLoggingEnabled = false
            logSwitch.isChecked = false
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        try {
            logWriter?.close()
            BleManager.disconnect()
        } catch (e: Exception) {
            Log.e(LOG_TAG, "Błąd zamykania zasobów: ${e.message}")
        }
    }
}