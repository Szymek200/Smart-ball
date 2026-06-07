package com.example.odbior_pomiarow

import java.util.Date


// Definicja stanów maszyny odbierającej dane przez UDP
enum class ReceiveState {
    NORMAL,
    EVENT_RECEIVING
}


// Typ zarejestrowanego zdarzenia w historii
enum class EntryType {
    HIT,
    FLIGHT
}

//kompletne dane o jednym historycznym zdarzeniu
data class HistoryEntry(
    val type: EntryType,
    val date: Date,
    val durationMs: Long,
    val samples: List<SampleData>,
    val peakValue: Float
)


//pojedyncza probka pomiarowa
data class SampleData(
    val ax: Float, val ay: Float, val az: Float,
    val gx: Float, val gy: Float, val gz: Float,
    val timestamp: Long,
    val isLive: Boolean = true
)