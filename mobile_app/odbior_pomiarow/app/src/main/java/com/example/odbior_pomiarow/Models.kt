package com.example.odbior_pomiarow

import java.util.Date

enum class ReceiveState {
    NORMAL,
    EVENT_RECEIVING
}

enum class EntryType {
    HIT,
    FLIGHT
}

//history item
data class HistoryEntry(
    val type: EntryType,
    val date: Date,
    val durationMs: Long,
    val samples: List<SampleData>,
    val peakValue: Float
)

data class SampleData(
    val ax: Float, val ay: Float, val az: Float,
    val gx: Float, val gy: Float, val gz: Float,
    val timestamp: Long,
    val isLive: Boolean = true
)