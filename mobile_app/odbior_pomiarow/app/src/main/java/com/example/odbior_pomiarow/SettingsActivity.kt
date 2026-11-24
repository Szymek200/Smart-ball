package com.example.odbior_pomiarow

import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.preference.PreferenceManager
import android.content.SharedPreferences

class SettingsActivity : AppCompatActivity() {

    // Pola tekstowe w ciele klasy – odczytywane i uzupełniane asynchronicznie po odebraniu danych z BLE
    private lateinit var etWakeupThs: EditText
    private lateinit var etHitThs: EditText
    private lateinit var etIdleTime: EditText
    private lateinit var etSleepThs: EditText

    // Pola metryk aplikacji
    private lateinit var etBallMass: EditText
    private lateinit var etAirThreshold: EditText
    private lateinit var etMinSpin: EditText

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        // 1. Wczytanie zapisanych wcześniej preferencji aplikacji
        MetricsCalculator.loadPreferences(this)

        // 2. NAJPIERW znajdujemy widoki (findViewById) - inaczej aplikacja się zawiesi!
        etWakeupThs = findViewById(R.id.etWakeupThreshold)
        etHitThs = findViewById(R.id.etHitThreshold)
        etIdleTime = findViewById(R.id.etIdleTime)
        etSleepThs = findViewById(R.id.etSleepThreshold)

        etBallMass = findViewById(R.id.etBallMass)
        etAirThreshold = findViewById(R.id.etAirThreshold)
        etMinSpin = findViewById(R.id.etMinSpin)

        // 3. TWORZYMY ZMIENNĄ sharedPrefs (tylko raz na całą funkcję onCreate)
        val sharedPrefs = PreferenceManager.getDefaultSharedPreferences(this)

        // 4. Ustawiamy zapisane wartości w polach
        etWakeupThs.setText(sharedPrefs.getString("ESP_WAKE_THS", "2.0"))
        etHitThs.setText(sharedPrefs.getString("ESP_HIT_THS", "10.0"))
        etIdleTime.setText(sharedPrefs.getString("ESP_IDLE_TIME", "300"))
        etSleepThs.setText(sharedPrefs.getString("ESP_SLEEP_THS", "1.5"))

        etBallMass.setText(MetricsCalculator.ballMassKg.toString())
        etAirThreshold.setText(MetricsCalculator.airThresholdMg.toString())
        etMinSpin.setText(MetricsCalculator.minSpinThresholdDps.toString())

        // 5. Przyciski - jako zmienne lokalne
        val btnSetWakeup = findViewById<Button>(R.id.btnSetWakeup)
        val btnSetHit = findViewById<Button>(R.id.btnSetHit)
        val btnSetIdle = findViewById<Button>(R.id.btnSetIdle)
        val btnSetSleepThs = findViewById<Button>(R.id.btnSetSleepThs)

        val btnSetBallMass = findViewById<Button>(R.id.btnSetBallMass)
        val btnSetAirThreshold = findViewById<Button>(R.id.btnSetAirThreshold)
        val btnSetMinSpin = findViewById<Button>(R.id.btnSetMinSpin)
        val btnBack = findViewById<Button>(R.id.btnBackFromSettings)

        // 6. Podpięcie nasłuchiwania na dane odczytane przez BLE
        BleManager.onDataReceivedListener = { response ->
            runOnUiThread {
                if (response.startsWith("CFG:")) {
                    // Format ramki: "CFG:wakeup:sleep:idle:hit"
                    val tokens = response.trim().split(":")
                    if (tokens.size >= 5) {
                        etWakeupThs.setText(tokens[1])
                        etSleepThs.setText(tokens[2])
                        etIdleTime.setText(tokens[3])
                        etHitThs.setText(tokens[4])

                        // Automatyczna synchronizacja progu uderzenia z MetricsCalculator
                        tokens[4].toFloatOrNull()?.let { hitG ->
                            MetricsCalculator.updateHitThresholdFromBle(hitG)
                        }

                        Toast.makeText(applicationContext, "Pobrano konfigurację przez BLE!", Toast.LENGTH_SHORT).show()
                    }
                }
            }
        }

        findViewById<Button>(R.id.btnClearCache).setOnClickListener {
            HistoryCacheManager.clearCache()
            Toast.makeText(this, "Wyczyszczono historię zdarzeń z pamięci RAM", Toast.LENGTH_SHORT).show()
        }

        // Pobierz aktualną konfigurację sprzętową po wejściu w widok
        fetchCurrentSettings()

        // 7. Obsługa parametrów BLE (wysyłanych do urządzenia ORAZ zapisywanych lokalnie z przedrostkiem ESP_)

        btnSetWakeup.setOnClickListener {
            val valueStr = etWakeupThs.text.toString().replace(",", ".")
            if (valueStr.toFloatOrNull() != null) {
                sendCommand("CMD:WAKE_THS:$valueStr")
                // Zapis lokalny z przedrostkiem dla BleManager
                sharedPrefs.edit().putString("ESP_WAKE_THS", valueStr).apply()
                Toast.makeText(this, "Zapisano próg wybudzania", Toast.LENGTH_SHORT).show()
            }
        }

        btnSetHit.setOnClickListener {
            val valueStr = etHitThs.text.toString().replace(",", ".")
            if (valueStr.toFloatOrNull() != null) {
                MetricsCalculator.updateHitThresholdFromBle(valueStr.toFloat())
                sendCommand("CMD:HIT_THS:$valueStr")
                // Zapis lokalny z przedrostkiem dla BleManager
                sharedPrefs.edit().putString("ESP_HIT_THS", valueStr).apply()
                Toast.makeText(this, "Zapisano próg zderzenia", Toast.LENGTH_SHORT).show()
            }
        }

        btnSetIdle.setOnClickListener {
            val valueStr = etIdleTime.text.toString()
            if (valueStr.toIntOrNull() != null) {
                sendCommand("CMD:IDLE_TIME:$valueStr")
                // Zapis lokalny z przedrostkiem dla BleManager
                sharedPrefs.edit().putString("ESP_IDLE_TIME", valueStr).apply()
                Toast.makeText(this, "Zapisano czas bezczynności", Toast.LENGTH_SHORT).show()
            }
        }

        btnSetSleepThs.setOnClickListener {
            val valueStr = etSleepThs.text.toString().replace(",", ".")
            if (valueStr.toFloatOrNull() != null) {
                sendCommand("CMD:SLEEP_THS:$valueStr")
                // Zapis lokalny z przedrostkiem dla BleManager
                sharedPrefs.edit().putString("ESP_SLEEP_THS", valueStr).apply()
                Toast.makeText(this, "Zapisano próg uśpienia", Toast.LENGTH_SHORT).show()
            }
        }

        // 8. Obsługa parametrów aplikacji (zapisywanych lokalnie w telefonie)
        btnSetBallMass.setOnClickListener {
            val mass = etBallMass.text.toString().replace(",", ".").toFloatOrNull()
            if (mass != null) {
                MetricsCalculator.savePreferences(
                    this,
                    mass,
                    MetricsCalculator.airThresholdMg,
                    MetricsCalculator.minSpinThresholdDps
                )
                Toast.makeText(this, "Zapisano masę piłki", Toast.LENGTH_SHORT).show()
            }
        }

        btnSetAirThreshold.setOnClickListener {
            val air = etAirThreshold.text.toString().replace(",", ".").toFloatOrNull()
            if (air != null) {
                MetricsCalculator.savePreferences(
                    this,
                    MetricsCalculator.ballMassKg,
                    air,
                    MetricsCalculator.minSpinThresholdDps
                )
                Toast.makeText(this, "Zapisano próg lotu", Toast.LENGTH_SHORT).show()
            }
        }

        btnSetMinSpin.setOnClickListener {
            val spin = etMinSpin.text.toString().replace(",", ".").toFloatOrNull()
            if (spin != null) {
                MetricsCalculator.savePreferences(
                    this,
                    MetricsCalculator.ballMassKg,
                    MetricsCalculator.airThresholdMg,
                    spin
                )
                Toast.makeText(this, "Zapisano próg rotacji", Toast.LENGTH_SHORT).show()
            }
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