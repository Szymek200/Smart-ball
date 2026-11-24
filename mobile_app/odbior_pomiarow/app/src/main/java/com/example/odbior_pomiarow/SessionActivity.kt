package com.example.odbior_pomiarow

import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import java.io.File
import androidx.preference.PreferenceManager
import android.content.SharedPreferences

class SessionActivity : AppCompatActivity() {

    private lateinit var etSessionName: EditText
    private lateinit var btnToggleSession: Button
    private lateinit var tvActiveSessionStatus: TextView
    private lateinit var sessionsContainer: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_session)

        findViewById<Button>(R.id.btnBackFromSessions).setOnClickListener { finish() }

        etSessionName = findViewById(R.id.etSessionName)
        btnToggleSession = findViewById(R.id.btnToggleSession)
        tvActiveSessionStatus = findViewById(R.id.tvActiveSessionStatus)
        sessionsContainer = findViewById(R.id.sessionsContainer)

        updateSessionUIState()

        btnToggleSession.setOnClickListener {
            if (SessionManager.isSessionActive) {
                SessionManager.stopCurrentSession()
                Toast.makeText(this, "Zakończono sesję treningową!", Toast.LENGTH_SHORT).show()
            } else {
                val name = etSessionName.text.toString().trim().ifEmpty { "Trening" }

                // 1. Pobieramy wszystkie preferencje
                val sharedPrefs = androidx.preference.PreferenceManager.getDefaultSharedPreferences(this)

                // 2. Automatyczna synchronizacja: BleManager sam znajdzie klucze "ESP_" i wyśle je do piłki
                BleManager.applyMasterConfiguration(sharedPrefs)

                // 3. Startujemy sesję (metadane zapiszą się same)
                val folder = SessionManager.startNewSession(this, name)
                Toast.makeText(this, "Zaktualizowano ESP i rozpoczęto: $folder", Toast.LENGTH_SHORT).show()
            }
            updateSessionUIState()
            refreshSessionsList()
        }

        refreshSessionsList()
    }

    private fun updateSessionUIState() {
        if (SessionManager.isSessionActive) {
            btnToggleSession.text = "ZAKOŃCZ OBECNY TRENING"
            btnToggleSession.setBackgroundColor(Color.parseColor("#D32F2F"))
            tvActiveSessionStatus.text = "Status: TRWA SESJA [${SessionManager.currentSessionFolderName}]"
            tvActiveSessionStatus.setTextColor(Color.parseColor("#388E3C"))
            etSessionName.isEnabled = false
        } else {
            btnToggleSession.text = "ROZPOCZNIJ NOWY TRENING"
            btnToggleSession.setBackgroundColor(Color.parseColor("#1976D2"))
            tvActiveSessionStatus.text = "Status: Brak aktywnego treningu"
            tvActiveSessionStatus.setTextColor(Color.GRAY)
            etSessionName.isEnabled = true
        }
    }

    private fun refreshSessionsList() {
        sessionsContainer.removeAllViews()
        val baseDir = SessionManager.getSessionsBaseDir(this)
        val sessionFolders = baseDir.listFiles { file -> file.isDirectory }?.sortedByDescending { it.lastModified() } ?: emptyList()

        if (sessionFolders.isEmpty()) {
            val emptyTv = TextView(this).apply {
                text = "Brak zapisanych treningów."
                setPadding(20, 20, 20, 20)
            }
            sessionsContainer.addView(emptyTv)
            return
        }

        sessionFolders.forEach { folder ->
            val cardLayout = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(24, 24, 24, 24)
                setBackgroundColor(Color.parseColor("#F0F0F0"))
                val params = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )
                params.setMargins(0, 0, 0, 16)
                layoutParams = params
            }

            val titleTv = TextView(this).apply {
                text = "📁 " + folder.name
                textSize = 15f
                setTypeface(null, android.graphics.Typeface.BOLD)
                setTextColor(Color.BLACK)
                setPadding(0, 0, 0, 12) // Dodano mały odstęp pod tytułem
            }

            val buttonsLayout = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )
            }

            val btnHistory = Button(this).apply {
                text = "HISTORIA"
                textSize = 11f
                setBackgroundColor(Color.parseColor("#3F51B5"))
                setTextColor(Color.WHITE)
                val params = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                params.setMargins(0, 0, 8, 0)
                layoutParams = params
                setOnClickListener {
                    val intent = Intent(this@SessionActivity, HistoryActivity::class.java).apply {
                        putExtra("SESSION_FOLDER_NAME", folder.name)
                    }
                    startActivity(intent)
                }
            }

            val btnShare = Button(this).apply {
                text = "CSV"
                textSize = 11f
                setBackgroundColor(Color.parseColor("#388E3C"))
                setTextColor(Color.WHITE)
                val params = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                params.setMargins(0, 0, 8, 0)
                layoutParams = params
                setOnClickListener {
                    SessionManager.shareSessionFile(this@SessionActivity, folder.name)
                }
            }

            val btnDelete = Button(this).apply {
                text = "USUŃ"
                textSize = 11f
                setBackgroundColor(Color.parseColor("#D32F2F"))
                setTextColor(Color.WHITE)
                val params = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                layoutParams = params
                setOnClickListener {
                    showDeleteConfirmationDialog(folder)
                }
            }

            buttonsLayout.addView(btnHistory)
            buttonsLayout.addView(btnShare)
            buttonsLayout.addView(btnDelete)

            cardLayout.addView(titleTv)
            cardLayout.addView(buttonsLayout)

            sessionsContainer.addView(cardLayout)
        }
    }

    // Funkcja do wyświetlania potwierdzenia i usuwania folderu
    private fun showDeleteConfirmationDialog(folder: File) {
        AlertDialog.Builder(this)
            .setTitle("Usunięcie treningu")
            .setMessage("Czy na pewno chcesz usunąć sesję '${folder.name}'?\n\nUsunięte zostaną wszystkie dane, historia i pliki CSV. Tej operacji nie można cofnąć.")
            .setPositiveButton("TAK, USUŃ") { _, _ ->
                if (folder.name == SessionManager.currentSessionFolderName) {
                    Toast.makeText(this, "Nie można usunąć trwającej sesji!", Toast.LENGTH_SHORT).show()
                } else {
                    // Usuwa folder wraz ze wszystkimi plikami w środku
                    val deleted = folder.deleteRecursively()
                    if (deleted) {
                        Toast.makeText(this, "Trening został usunięty.", Toast.LENGTH_SHORT).show()
                        refreshSessionsList() // Odświeżamy widok
                    } else {
                        Toast.makeText(this, "Błąd podczas usuwania treningu.", Toast.LENGTH_SHORT).show()
                    }
                }
            }
            .setNegativeButton("ANULUJ", null) // Anuluj po prostu zamyka okno
            .show()
    }
}