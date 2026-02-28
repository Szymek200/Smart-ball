package com.example.odbior_pomiarow

import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.Switch
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress

class SettingsActivity : AppCompatActivity() {

    // Adres IP urządzenia
    private val ESP_IP = "192.168.4.1"
    private val ESP_PORT = 5000

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        // --- POBRANIE REFERENCJI DO WIDOKÓW ---
        val etWakeup = findViewById<EditText>(R.id.etWakeupThreshold) // Próg wybudzenia (sprzętowy)
        val btnWakeup = findViewById<Button>(R.id.btnSetWakeup)

        val etHit = findViewById<EditText>(R.id.etHitThreshold)       // Próg wykrycia uderzenia (programowy)
        val btnHit = findViewById<Button>(R.id.btnSetHit)

        val etEnd = findViewById<EditText>(R.id.etEndThreshold)       // Próg końca uderzenia
        val btnSetEnd = findViewById<Button>(R.id.btnSetEnd)

        val etIdle = findViewById<EditText>(R.id.etIdleTime)          // Czas bezczynności do uśpienia
        val btnSetIdle = findViewById<Button>(R.id.btnSetIdle)

        val swSleep = findViewById<Switch>(R.id.swSleepEnable)        // Przełącznik trybu uśpienia
        val btnBack = findViewById<Button>(R.id.btnBackFromSettings)

        // OBSŁUGA ZDARZEŃ

        // Usypianie (CMD:SLEEP_EN)
        swSleep.setOnCheckedChangeListener { _, isChecked ->
            if (isChecked) {
                swSleep.text = "Automatyczne usypianie włączone"
                sendUdpCommand("CMD:SLEEP_EN:1")
            } else {
                swSleep.text = "Automatyczne usypianie WYŁĄCZONE (Tryb ciągły)"
                sendUdpCommand("CMD:SLEEP_EN:0")
                Toast.makeText(this, "Uwaga: Bateria będzie się szybciej zużywać!", Toast.LENGTH_SHORT).show()
            }
        }

        // Prog końca zdarzenia (CMD:END)
        btnSetEnd.setOnClickListener {
            val valueStr = etEnd.text.toString().replace(",", ".") // Normalizacja separatora dziesiętnego
            val value = valueStr.toFloatOrNull()

            if (value != null && value > 0.5f) {
                sendUdpCommand("CMD:END:$valueStr")
            } else {
                Toast.makeText(this, "Podaj poprawną wartość (np. 1.1)", Toast.LENGTH_SHORT).show()
            }
        }

        // Czas bezczynności (CMD:IDLE)
        btnSetIdle.setOnClickListener {
            val valueStr = etIdle.text.toString()
            val value = valueStr.toIntOrNull()

            if (value != null && value >= 5) {
                sendUdpCommand("CMD:IDLE:$value")
            } else {
                Toast.makeText(this, "Minimum 5 sekund!", Toast.LENGTH_SHORT).show()
            }
        }

        // Prog wybudzenia sprzętowego (CMD:THS)
        btnWakeup.setOnClickListener {
            val valueStr = etWakeup.text.toString()
            if (valueStr.isNotEmpty()) {
                val value = valueStr.toIntOrNull()
                // Rejestr akcelerometru przyjmuje wartości całkowite
                if (value != null && value in 1..127) {
                    sendUdpCommand("CMD:THS:$value")
                } else {
                    Toast.makeText(this, "Podaj liczbę całkowitą 1-127", Toast.LENGTH_SHORT).show()
                }
            }
        }

        // Progu uderzenia (CMD:HIT)
        btnHit.setOnClickListener {
            val valueStr = etHit.text.toString()
            if (valueStr.isNotEmpty()) {
                val cleanVal = valueStr.replace(",", ".")
                val value = cleanVal.toFloatOrNull()

                if (value != null && value > 0.1) {
                    sendUdpCommand("CMD:HIT:$cleanVal")
                } else {
                    Toast.makeText(this, "Podaj poprawną wartość G (np. 1.5)", Toast.LENGTH_SHORT).show()
                }
            }
        }

        btnBack.setOnClickListener { finish() }
    }

    // Funkcja pomocnicza do wysyłania komend UDP w tle
    private fun sendUdpCommand(command: String) {
        // Używamy Dispatchers.IO dla operacji sieciowych
        CoroutineScope(Dispatchers.IO).launch {
            try {
                val socket = DatagramSocket()
                val address = InetAddress.getByName(ESP_IP)
                val buffer = command.toByteArray()

                // Tworzenie i wysłanie pakietu
                val packet = DatagramPacket(buffer, buffer.size, address, ESP_PORT)
                socket.send(packet)
                socket.close()

                // Powiadomienie użytkownika na głównym wątku
                runOnUiThread {
                    Toast.makeText(applicationContext, "Wysłano: $command", Toast.LENGTH_SHORT).show()
                }
            } catch (e: Exception) {
                e.printStackTrace()
                runOnUiThread {
                    Toast.makeText(applicationContext, "Błąd wysyłania: ${e.message}", Toast.LENGTH_LONG).show()
                }
            }
        }
    }
}