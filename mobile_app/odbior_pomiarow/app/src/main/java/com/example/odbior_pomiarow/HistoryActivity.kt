package com.example.odbior_pomiarow

import android.graphics.Color
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.components.YAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import com.github.mikephil.charting.formatter.ValueFormatter
import java.text.SimpleDateFormat
import java.util.*
import kotlin.math.sqrt

class HistoryActivity : AppCompatActivity() {

    private lateinit var historyChart: LineChart // Wykres

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_history)

        findViewById<Button>(R.id.btnBack)?.setOnClickListener { finish() } // Zamknij aktywność

        historyChart = findViewById(R.id.historyChart)
        setupHistoryChart() // Konfiguracja wyglądu wykresu

        //kontener zawierajacy wszystkie zdarzenia
        val container = findViewById<LinearLayout>(R.id.historyListContainer)
        val timeFormatter = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

        // GENEROWANIE LISTY ZDARZEŃ
        // Iterujemy po globalnej liście historii z MainActivity
        MainActivity.fullHitHistory.forEachIndexed { index, entry ->
            val tv = TextView(this).apply {
                // Formatowanie tekstu w zależności od typu (Uderzenie vs Lot)
                if (entry.type == EntryType.HIT) {
                    text = "[${timeFormatter.format(entry.date)}] 💥 ZDERZENIE: ${String.format("%.2f", entry.peakValue/1000f)} G | ${entry.durationMs} ms"
                    // Naprzemienne kolorowanie tła dla czytelności
                    setBackgroundColor(if (index % 2 == 0) Color.parseColor("#FFEBEB") else Color.WHITE)
                } else {
                    text = "[${timeFormatter.format(entry.date)}] ✈️ LOT: ${entry.durationMs} ms | Rot: ${String.format("%.0f", entry.peakValue)} dps"
                    setBackgroundColor(if (index % 2 == 0) Color.parseColor("#EBF5FF") else Color.WHITE)
                }

                setPadding(30, 30, 30, 30)
                textSize = 16f

                // Kliknięcie w element listy rysuje wykres z danych tego elementu
                setOnClickListener {
                    drawHistoryChart(entry.samples)
                    Toast.makeText(context, "Szczegóły zdarzenia", Toast.LENGTH_SHORT).show()
                }
            }
            container.addView(tv) // Dodanie widoku tekstowego do layoutu
        }
    }

    // Funkcja rysująca wykres na podstawie historycznych próbek
    private fun drawHistoryChart(samples: List<SampleData>) {
        if (samples.isEmpty()) return

        val accelEntries = mutableListOf<Entry>()
        val gyroEntries = mutableListOf<Entry>()
        val startTs = samples.first().timestamp

        samples.forEach { s ->
            val x = (s.timestamp - startTs) / 1000000f // Czas w sekundach

            // Obliczanie magnitudy wektorów
            val aMag = sqrt((s.ax * s.ax + s.ay * s.ay + s.az * s.az).toDouble()).toFloat() / 1000f
            accelEntries.add(Entry(x, aMag))

            val gMag = sqrt((s.gx * s.gx + s.gy * s.gy + s.gz * s.gz).toDouble()).toFloat()
            gyroEntries.add(Entry(x, gMag))
        }

        // Konfiguracja serii danych
        val setA = LineDataSet(accelEntries, "Siła [G]").apply {
            color = Color.RED
            axisDependency = YAxis.AxisDependency.LEFT // Przypięcie do lewej osi
            setDrawCircles(false)
            setDrawValues(false)
        }
        val setG = LineDataSet(gyroEntries, "Rotacja [dps]").apply {
            color = Color.BLUE
            axisDependency = YAxis.AxisDependency.RIGHT // Przypięcie do prawej osi
            setDrawCircles(false)
            setDrawValues(false)
        }

        historyChart.data = LineData(setA, setG)
        historyChart.invalidate() // Odświeżenie wykresu
    }

    // Ustawienia wizualne wykresu (osie, formatowanie)
    private fun setupHistoryChart() {
        historyChart.apply {
            description.isEnabled = false
            setTouchEnabled(true)
            setPinchZoom(true)
            setBackgroundColor(Color.WHITE)

            // Formatowanie osi X (czas w sekundach)
            xAxis.apply {
                position = XAxis.XAxisPosition.BOTTOM
                setDrawGridLines(true)
                valueFormatter = object : ValueFormatter() {
                    override fun getFormattedValue(value: Float): String {
                        return String.format("%.2fs", value)
                    }
                }
            }
            // Lewa oś (czerwona)
            axisLeft.apply {
                textColor = Color.RED
                axisMinimum = 0f
            }
            // Prawa oś (niebieska)
            axisRight.apply {
                isEnabled = true
                textColor = Color.BLUE
                axisMinimum = 0f
                setDrawGridLines(false)
            }
        }
    }
}