package com.example.odbior_pomiarow

import java.util.ArrayDeque

object HistoryCacheManager {

    private const val MAX_CACHE_ENTRIES = 500

    private val _history = ArrayDeque<HistoryEntry>()

    // Publiczna lista (tylko do odczytu) dla widoków
    val history: List<HistoryEntry>
        get() = _history.toList()

    fun addEvent(entry: HistoryEntry) {
        // 1. Dodajemy na początek kolejki RAM (najnowsze uderzenia na górze)
        _history.addFirst(entry)

        // Usuwamy najstarsze uderzenie z końca, jeśli przekroczymy limit
        if (_history.size > MAX_CACHE_ENTRIES) {
            _history.removeLast()
        }

        // 2. Automatyczny zapis na dysk, jeśli trwa jakakolwiek sesja
        SessionManager.saveEventToSession(entry)
    }

    // Ta funkcja jest potrzebna dla HistoryActivity do wczytywania historycznych sesji z pliku


    fun clearCache() {
        _history.clear()
    }
}