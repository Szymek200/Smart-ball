package com.example.odbior_pomiarow

import android.graphics.Color
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import java.util.Date
import kotlin.math.sqrt

class ShotDetailsActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_shot_details)

        MetricsCalculator.loadPreferences(this)

        findViewById<Button>(R.id.btnBackFromDetails).setOnClickListener { finish() }


        val historyEntry = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra("EXTRA_HIT_ENTRY", HistoryEntry::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra("EXTRA_HIT_ENTRY") as? HistoryEntry
        }

        // Jeśli obiekt nie dotarł, bezpiecznie zamykamy
        if (historyEntry == null) {
            Toast.makeText(this, "Błąd wczytywania szczegółów zdarzenia", Toast.LENGTH_SHORT).show()
            finish()
            return
        }

        val selectedSamples = historyEntry.samples
        val peakValue = historyEntry.peakValue
        val entryType = historyEntry.type

        val tvHeader = findViewById<TextView>(R.id.tvDetailsHeader)
        val tvStats = findViewById<TextView>(R.id.tvDetailsMetrics)
        val chart = findViewById<LineChart>(R.id.detailsChart)

        if (entryType == EntryType.HIT) {

            val exitVelocity = MetricsCalculator.calculateExitVelocityFromPacketKmH(selectedSamples)
            val contactTime = MetricsCalculator.calculateContactTimeMs(selectedSamples)
            val smashFactor = MetricsCalculator.calculateSmashFactor(selectedSamples, peakValue)
            val angles = MetricsCalculator.calculateImpactAngle(selectedSamples)

            // Wyliczenie RPM dla piku zderzenia
            val peakSample = selectedSamples.maxByOrNull { sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()) } ?: selectedSamples[0]
            val spinAtImpactRPM = MetricsCalculator.calculateSpinRPM(peakSample)

            // Energia kinetyczna tylko dla tego pojedynczego strzału
            val tempHistory = listOf(HistoryEntry(EntryType.HIT, Date(), 0, selectedSamples, peakValue))
            val energyJoules = MetricsCalculator.calculateTotalEnergyExpenditureJ(tempHistory)

            tvHeader.text = String.format("ANALIZA STRZAŁU: %.2f G", peakValue / 1000f)
            tvStats.text = """
                💥 Szczytowe przeciążenie: ${String.format("%.2f", peakValue / 1000f)} G
                ⚽ Prędkość początkowa: ${String.format("%.1f", exitVelocity)} km/h
                ⏱️ Czas kontaktu z butem: $contactTime ms
                💎 Wskaźnik czystości (Smash): ${String.format("%.2f", smashFactor)}
                📐 Kąt uderzenia (Poziom / Azymut): ${String.format("%.1f", angles.first)}°
                📈 Kąt uderzenia (Pion / Podbicie): ${String.format("%.1f", angles.second)}°
                ⚡ Energia kinetyczna: ${String.format("%.1f", energyJoules)} J
                --- EFEKT PO UDERZENIU ---
                🔄 Rotacja w piku zderzenia: ${String.format("%.0f", spinAtImpactRPM)} RPM
            """.trimIndent()
        } else {
            val hangTime = MetricsCalculator.calculateHangTimeSeconds(selectedSamples)
            val spinAxis = MetricsCalculator.determineSpinAxisAndMagnus(selectedSamples)

            val avgRotationDps = MetricsCalculator.calculateAverageRotationDps(selectedSamples)
            val avgRotationRPM = MetricsCalculator.calculateAverageRotationRPM(selectedSamples)

            val initialVelocityIfAny = MetricsCalculator.calculateExitVelocityFromPacketKmH(selectedSamples)

            tvHeader.text = "ANALIZA FAZY LOTU PIŁKI"
            tvStats.text = """
                ✈️ Czas zawieszenia (Hang Time): ${String.format("%.2f", hangTime)} sekundy
                🔄 Średnia rotacja: ${String.format("%.0f", avgRotationRPM)} RPM (${String.format("%.1f", avgRotationDps)} dps)
                🧭 Maksymalna rotacja: ${String.format("%.0f", peakValue / 6f)} RPM
                🌀 Charakterystyka osi obrotu: $spinAxis
                --- DODATKOWO ---
                ⚽ Prędkość wybicia w tej ramce: ${String.format("%.1f", initialVelocityIfAny)} km/h
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