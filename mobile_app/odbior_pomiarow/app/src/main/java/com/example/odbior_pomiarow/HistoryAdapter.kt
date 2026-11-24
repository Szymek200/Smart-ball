package com.example.odbior_pomiarow

import android.graphics.Color
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import java.text.SimpleDateFormat
import java.util.Locale

class HistoryAdapter(
    private val entries: List<HistoryEntry>,
    private val onItemClick: (Int) -> Unit // Zwracamy po prostu INDEKS klikniętego elementu
) : RecyclerView.Adapter<HistoryAdapter.ViewHolder>() {

    private val timeFormatter = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        val tvEntry: TextView = view.findViewById(android.R.id.text1)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        //layout dla pojedynczej linii tekstu
        val view = LayoutInflater.from(parent.context)
            .inflate(android.R.layout.simple_list_item_1, parent, false)
        return ViewHolder(view)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val entry = entries[position]
        val tv = holder.tvEntry

        // Formatowanie tekstu i koloru tła z HistoryActivity
        if (entry.type == EntryType.HIT) {
            tv.text = "[${timeFormatter.format(entry.date)}] \uD83D\uDCA5 ZDERZENIE: ${String.format("%.2f", entry.peakValue / 1000f)} G | ${entry.durationMs} ms"
            tv.setBackgroundColor(if (position % 2 == 0) Color.parseColor("#FFEBEB") else Color.WHITE)
        } else {
            tv.text = "[${timeFormatter.format(entry.date)}] ✈\uFE0F LOT: ${entry.durationMs} ms | Maks. Rot: ${String.format("%.0f", entry.peakValue / 6f)} RPM"
            tv.setBackgroundColor(if (position % 2 == 0) Color.parseColor("#EBF5FF") else Color.WHITE)
        }

        tv.setPadding(30, 30, 30, 30)
        tv.textSize = 16f

        // Kliknięcie odsyła INDEKS (position) do wywołującego activity
        holder.itemView.setOnClickListener {
            onItemClick(position)
        }
    }

    override fun getItemCount() = entries.size
}