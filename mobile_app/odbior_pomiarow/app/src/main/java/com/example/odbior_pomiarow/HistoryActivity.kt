package com.example.odbior_pomiarow

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.*
import java.io.File

class HistoryActivity : AppCompatActivity() {

    private lateinit var recyclerView: RecyclerView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_history)

        findViewById<Button>(R.id.btnBack)?.setOnClickListener { finish() }

        recyclerView = findViewById(R.id.recyclerViewHistory)
        recyclerView.layoutManager = LinearLayoutManager(this)

        // Odczytujemy paczkę (nazwę sesji), jeśli ekran wywołano z menedżera
        val sessionFolderName = intent.getStringExtra("SESSION_FOLDER_NAME")

        if (sessionFolderName == null) {
            // TRYB 1: Ekran wywołany z głównego menu - pokazujemy bieżącą pamięć RAM
            setupAdapter(HistoryCacheManager.history)
        } else {
            // TRYB 2: Ekran wywołany z Menedżera Sesji - wczytujemy plik JSONL w tle
            Toast.makeText(this, "Wczytywanie historii sesji...", Toast.LENGTH_SHORT).show()
            loadHistoryFromMetadata(sessionFolderName)
        }
    }

    // Wyciągnęliśmy to do osobnej metody, żeby móc odświeżyć adapter po wczytaniu pliku
    private fun setupAdapter(eventsToShow: List<HistoryEntry>) {
        val adapter = HistoryAdapter(eventsToShow) { clickedIndex ->
            // Pobieramy kliknięty element z przekazanej listy
            val clickedEntry = eventsToShow[clickedIndex]
            val intent = Intent(this, ShotDetailsActivity::class.java).apply {
                putExtra("EXTRA_HIT_ENTRY", clickedEntry)
            }
            startActivity(intent)
        }
        recyclerView.adapter = adapter
    }

    private fun loadHistoryFromMetadata(folderName: String) {
        // Uruchamiamy wczytywanie w tle, aby nie zamrozić ekranu telefonu
        CoroutineScope(Dispatchers.IO).launch {
            val baseDir = SessionManager.getSessionsBaseDir(this@HistoryActivity)
            val sessionFolder = File(baseDir, folderName)
            val eventsFile = File(sessionFolder, "events.jsonl")

            if (!eventsFile.exists()) {
                withContext(Dispatchers.Main) {
                    Toast.makeText(this@HistoryActivity, "Brak zarejestrowanych uderzeń w tej sesji.", Toast.LENGTH_SHORT).show()
                }
                return@launch
            }

            val parsedEvents = mutableListOf<HistoryEntry>()
            val gson = com.google.gson.Gson()

            try {
                // Odczytujemy szybki i lekki plik JSONL (każda linia to jeden pełny obiekt)
                eventsFile.forEachLine { line ->
                    if (line.isNotBlank()) {
                        val entry = gson.fromJson(line, HistoryEntry::class.java)
                        parsedEvents.add(entry)
                    }
                }

                withContext(Dispatchers.Main) {
                    // Wczytujemy zdarzenia do pamięci i odświeżamy widok
                    setupAdapter(parsedEvents.reversed())
                    Toast.makeText(this@HistoryActivity, "Wczytano ${parsedEvents.size} zdarzeń", Toast.LENGTH_SHORT).show()
                }

            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    Toast.makeText(this@HistoryActivity, "Błąd parsowania pliku metadanych", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }
}