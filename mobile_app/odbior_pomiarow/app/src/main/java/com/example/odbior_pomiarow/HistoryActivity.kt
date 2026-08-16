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
import android.content.Intent

class HistoryActivity : AppCompatActivity() {

    private lateinit var historyChart: LineChart

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_history)
        findViewById<Button>(R.id.btnBack)?.setOnClickListener { finish() }
        val container = findViewById<LinearLayout>(R.id.historyListContainer)
        val timeFormatter = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

        // GENEROWANIE LISTY ZDARZEŃ Z HISTORII SESJI
        MainActivity.fullHitHistory.forEachIndexed { index, entry ->
            val tv = TextView(this).apply {
                if (entry.type == EntryType.HIT) {
                    text = "[${timeFormatter.format(entry.date)}] 💥 ZDERZENIE: ${
                        String.format(
                            "%.2f",
                            entry.peakValue / 1000f
                        )
                    } G | ${entry.durationMs} ms"
                    setBackgroundColor(if (index % 2 == 0) Color.parseColor("#FFEBEB") else Color.WHITE)
                } else {
                    text =
                        "[${timeFormatter.format(entry.date)}] ✈️ LOT: ${entry.durationMs} ms | Maks. Rot: ${
                            String.format(
                                "%.0f",
                                entry.peakValue / 6f
                            )
                        } RPM"
                    setBackgroundColor(if (index % 2 == 0) Color.parseColor("#EBF5FF") else Color.WHITE)
                }

                setPadding(30, 30, 30, 30)
                textSize = 16f

                // Klikniecie na element przenosi do ShotDetailsActivity
                setOnClickListener {
                    ShotDetailsActivity.selectedSamples = entry.samples
                    ShotDetailsActivity.peakValue = entry.peakValue
                    ShotDetailsActivity.entryType = entry.type

                    val intent = Intent(context, ShotDetailsActivity::class.java)
                    startActivity(intent)
                }
            }
            container.addView(tv)
        }
    }
}


