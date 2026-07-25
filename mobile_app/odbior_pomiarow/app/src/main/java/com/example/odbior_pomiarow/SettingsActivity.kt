package com.example.odbior_pomiarow

import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class SettingsActivity : AppCompatActivity() {

    private lateinit var etWakeupThs: EditText
    private lateinit var etHitThs: EditText
    private lateinit var etIdleTime: EditText
    private lateinit var etSleepThs: EditText

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        etWakeupThs = findViewById(R.id.etWakeupThreshold)
        etHitThs = findViewById(R.id.etHitThreshold)
        etIdleTime = findViewById(R.id.etIdleTime)
        etSleepThs = findViewById(R.id.etSleepThreshold)

        val btnSetWakeup = findViewById<Button>(R.id.btnSetWakeup)
        val btnSetHit = findViewById<Button>(R.id.btnSetHit)
        val btnSetIdle = findViewById<Button>(R.id.btnSetIdle)
        val btnSetSleepThs = findViewById<Button>(R.id.btnSetSleepThs)
        val btnBack = findViewById<Button>(R.id.btnBackFromSettings)

        // Podpięcie nasłuchiwania na dane odczytane przez BLE
        BleManager.onDataReceivedListener = { response ->
            runOnUiThread {
                if (response.startsWith("CFG:")) {
                    // Parsowanie "CFG:1.50:0.050:120:4.5"
                    val tokens = response.trim().split(":")
                    if (tokens.size >= 5) {
                        etWakeupThs.setText(tokens[1])
                        etSleepThs.setText(tokens[2])
                        etIdleTime.setText(tokens[3])
                        etHitThs.setText(tokens[4])
                        Toast.makeText(applicationContext, "Pobrano konfigurację przez BLE!", Toast.LENGTH_SHORT).show()
                    }
                }
            }
        }

        // Pobierz aktualną konfigurację po wejściu w widok
        fetchCurrentSettings()

        // Przysyłanie zmian parametrów
        btnSetWakeup.setOnClickListener {
            val valueStr = etWakeupThs.text.toString().replace(",", ".")
            if (valueStr.toFloatOrNull() != null) sendCommand("CMD:WAKE_THS:$valueStr")
        }

        btnSetHit.setOnClickListener {
            val valueStr = etHitThs.text.toString().replace(",", ".")
            if (valueStr.toFloatOrNull() != null) sendCommand("CMD:HIT_THS:$valueStr")
        }

        btnSetIdle.setOnClickListener {
            val valueStr = etIdleTime.text.toString()
            if (valueStr.toIntOrNull() != null) sendCommand("CMD:IDLE_TIME:$valueStr")
        }

        btnSetSleepThs.setOnClickListener {
            val valueStr = etSleepThs.text.toString().replace(",", ".")
            if (valueStr.toFloatOrNull() != null) sendCommand("CMD:SLEEP_THS:$valueStr")
        }

        btnBack.setOnClickListener { finish() }
    }

    private fun fetchCurrentSettings() {
        if (BleManager.isConnected) {
            BleManager.readConfiguration()
        } else {
            Toast.makeText(this, "Brak połączenia BLE z urządzeniem", Toast.LENGTH_SHORT).show()
        }
    }

    private fun sendCommand(command: String) {
        if (BleManager.isConnected) {
            val success = BleManager.sendCommand(command)
            if (success) {
                Toast.makeText(this, "Wysłano komendę BLE: $command", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "Błąd wysyłania komendy BLE", Toast.LENGTH_SHORT).show()
            }
        } else {
            Toast.makeText(this, "Brak połączenia BLE!", Toast.LENGTH_SHORT).show()
        }
    }

    companion object {
        fun isBleConnected(): Boolean {
            return BleManager.isConnected
        }

        fun sendSoundCommandBle(turnOn: Boolean): Boolean {
            val command = if (turnOn) "CMD:PLAY_SOUND" else "CMD:STOP_SOUND"
            return BleManager.sendCommand(command)
        }
    }
}