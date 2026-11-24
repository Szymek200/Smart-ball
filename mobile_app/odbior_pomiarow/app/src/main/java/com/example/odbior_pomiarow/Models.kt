package com.example.odbior_pomiarow

import android.os.Parcelable
import kotlinx.parcelize.Parcelize
import java.util.Date

enum class EntryType {
    HIT,
    FLIGHT
}

//history item
@Parcelize
data class HistoryEntry(
    val type: EntryType,
    val date: Date,
    val durationMs: Long,
    val samples: List<SampleData>,
    val peakValue: Float
): Parcelable

@Parcelize //send data between views - serializes data
data class SampleData(
    val ax: Float, val ay: Float, val az: Float,
    val gx: Float, val gy: Float, val gz: Float,
    val timestamp: Long,
    val isLive: Boolean = true //false when HIT and buffered data is sent
) : Parcelable