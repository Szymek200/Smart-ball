package com.example.odbior_pomiarow

import android.graphics.Color
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import kotlin.math.sqrt

class ShotDetailsActivity : AppCompatActivity() {

    companion object {
        // Przechowywanie danych przekazanych z zewnątrz (MainActivity lub HistoryActivity)
        var selectedSamples: List<SampleData> = emptyList()
        var peakValue: Float = 0f
        var entryType: EntryType = EntryType.HIT // NOWOŚĆ: Przekazujemy typ zdarzenia
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_shot_details)

        findViewById<Button>(R.id.btnBackFromDetails).setOnClickListener { finish() }

        val tvHeader = findViewById<TextView>(R.id.tvDetailsHeader)
        val tvStats = findViewById<TextView>(R.id.tvDetailsMetrics)
        val chart = findViewById<LineChart>(R.id.detailsChart)

        if (entryType == EntryType.HIT) {
            // ==========================================
            // LOGIKA DLA UDERZENIA (💥 CRASH / SHOT PROFILE)
            // ==========================================
            val exitVelocity = MetricsCalculator.calculateExitVelocityKmH(selectedSamples)
            val contactTime = MetricsCalculator.calculateContactTimeMs(selectedSamples)
            val smashFactor = MetricsCalculator.calculateSmashFactor(selectedSamples, peakValue)
            val angles = MetricsCalculator.calculateImpactAngle(selectedSamples)

            tvHeader.text = String.format("ANALIZA STRZAŁU: %.2f G", peakValue / 1000f)
            tvStats.text = """
                💥 Szczytowe przeciążenie: ${String.format("%.2f", peakValue / 1000f)} G
                ⚽ Prędkość początkowa: ${String.format("%.1f", exitVelocity)} km/h
                ⏱️ Czas kontaktu z butem: $contactTime ms
                💎 Wskaźnik czystości (Smash): ${String.format("%.2f", smashFactor)}
                📐 Kąt uderzenia (Poziom / Azymut): ${String.format("%.1f", angles.first)}°
                📈 Kąt uderzenia (Pion / Podbicie): ${String.format("%.1f", angles.second)}°
            """.trimIndent()
        } else {
            // ==========================================
            // LOGIKA DLA LOTU (✈️ FLIGHT / SPIN ANALYSIS)
            // ==========================================
            val hangTime = MetricsCalculator.calculateHangTimeSeconds(selectedSamples)
            val spinAxis = MetricsCalculator.determineSpinAxisAndMagnus(selectedSamples)

            // Średni obrót wyliczony z całej serii próbek lotu
            val avgRotation = selectedSamples.map {
                sqrt((it.gx * it.gx + it.gy * it.gy + it.gz * it.gz).toDouble()).toFloat()
            }.average().toFloat()

            tvHeader.text = "ANALIZA FAZY LOTU PIŁKI"
            tvStats.text = """
                ✈️ Czas zawieszenia (Hang Time): ${String.format("%.2f", hangTime)} sekundy
                🔄 Średnia prędkość obrotowa: ${String.format("%.0f", avgRotation / 6f)} RPM (${String.format("%.1f", avgRotation)} dps)
                🧭 Maksymalna rotacja: ${String.format("%.0f", peakValue / 6f)} RPM
                🌀 Charakterystyka osi obrotu: $spinAxis
            """.trimIndent()
        }

        drawCollisionChart(chart, selectedSamples)
    }

    private fun drawCollisionChart(lineChart: LineChart, samples: List<SampleData>) {
        if (samples.isEmpty()) return
        val accelEntries = mutableListOf<Entry>()
        val gyroEntries = mutableListOf<Entry>()

        samples.forEachIndexed { index, s ->
            val x = index * 0.010f // Krok czasowy 10ms
            val aMag = sqrt((s.ax * s.ax + s.ay * s.ay + s.az * s.az).toDouble()).toFloat() / 1000f
            accelEntries.add(Entry(x, aMag))
            val gMag = sqrt((s.gx * s.gx + s.gy * s.gy + s.gz * s.gz).toDouble()).toFloat()
            gyroEntries.add(Entry(x, gMag))
        }

        val setA = LineDataSet(accelEntries, "Siła [G]").apply {
            color = Color.RED; setDrawCircles(false); lineWidth = 2.5f; setDrawValues(false)
        }
        val setG = LineDataSet(gyroEntries, "Rotacja [dps]").apply {
            color = Color.BLUE; setDrawCircles(false); lineWidth = 2.5f; setDrawValues(false)
            axisDependency = com.github.mikephil.charting.components.YAxis.AxisDependency.RIGHT
        }

        lineChart.apply {
            description.isEnabled = false; setTouchEnabled(true); setBackgroundColor(Color.WHITE)
            axisLeft.textColor = Color.RED
            axisRight.apply { isEnabled = true; textColor = Color.BLUE }
            data = LineData(setA, setG)
            invalidate()
        }
    }
}