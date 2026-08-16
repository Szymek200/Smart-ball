package com.example.odbior_pomiarow

import java.util.Date
import kotlin.math.abs
import kotlin.math.acos
import kotlin.math.atan2
import kotlin.math.pow
import kotlin.math.sqrt

object MetricsCalculator {

    const val BALL_MASS_KG = 0.43f
    /**
     * Oblicza prędkość obrotową (Spin Rate) w RPM dla pojedynczej próbki.
     * Żyroskop zwraca stopnie na sekundę (dps).
     */
    fun calculateSpinRPM(sample: SampleData): Float {
        //double for sqrt, float in the end
        val magnitudeDps = sqrt((sample.gx * sample.gx + sample.gy * sample.gy + sample.gz * sample.gz).toDouble()).toFloat()
        return magnitudeDps / 6.0f // (magnitudeDps / 360.0f) * 60.0f
    }

    /**
     * Określa dominującą oś rotacji oraz jej charakterystykę fizyczną (Top-spin, Back-spin, Side-spin).
     */
    fun determineSpinAxisAndMagnus(samples: List<SampleData>): String {
        if (samples.isEmpty()) return "Brak danych"

        // Wyliczamy średnie wartości dla osi z całego lotu
        val avgGx = samples.map { it.gx }.average().toFloat()
        val avgGy = samples.map { it.gy }.average().toFloat()
        val avgGz = samples.map { it.gz }.average().toFloat()

        val total = sqrt((avgGx * avgGx + avgGy * avgGy + avgGz * avgGz).toDouble()).toFloat()
        if (total < 10.0f) return "Brak rotacji"

        // Interpretacja osi żyroskopu w piłce:
        // Gx - obrót wokół osi poprzecznej (Top-spin / Back-spin)
        // Gy - obrót wokół osi pionowej (Side-spin / podkręcenie lewo-prawo)
        // Gz - obrót wokół osi podłużnej (Rifle-spin / rotacja śrubowa)
        return when {
            abs(avgGx) > abs(avgGy) && abs(avgGx) > abs(avgGz) -> if (avgGx > 0) "Top-spin (Opadająca)" else "Back-spin (Nośna)"
            abs(avgGy) > abs(avgGx) && abs(avgGy) > abs(avgGz) -> if (avgGy > 0) "Side-spin (Prawo)" else "Side-spin (Lewo)"
            else -> "Rifle-spin (Śrubowa)"
        }
    }

    /**
     * Oblicza czas zawieszenia w powietrzu (Hang Time) na podstawie próbek lotu.
     * Wykrywa stan nieważkości (gdy wypadkowe przyspieszenie IMU odpina grawitację i spada blisko 0 mg).
     */
    fun calculateHangTimeSeconds(flightSamples: List<SampleData>): Float {
        // Filtrujemy próbki, gdzie wypadkowe przyspieszenie jest mniejsze niż 300 mg (swobodny lot)
        val airSamplesCount = flightSamples.count {
            val aMag = sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()).toFloat()
            aMag < 300f // próg 300 mg
        }
        return airSamplesCount * 0.010f // Każda próbka w systemie to 10 ms
    }
    /**
     * Prędkość początkowa piłki (Exit Velocity) w km/h.
     * Wyliczana poprzez numeryczne całkowanie (metodą prostokątów) pola pod wykresem przyspieszenia w fazie zderzenia.
     */
    fun calculateExitVelocityKmH(hitSamples: List<SampleData>): Float {
        if (hitSamples.isEmpty()) return 0f

        var deltaVelocityMS = 0f
        // Całkujemy tylko próbki, które rejestrują realne uderzenie (wycinamy szum tła)
        hitSamples.forEach { s ->
            // Konwersja przyspieszenia IMU z mg na m/s^2 (1000 mg = 1G = 9.81 m/s^2)
            val axMS2 = (s.ax / 1000f) * 9.81f
            val ayMS2 = (s.ay / 1000f) * 9.81f
            val azMS2 = (s.az / 1000f) * 9.81f
            val totalAccMS2 = sqrt((axMS2 * axMS2 + ayMS2 * ayMS2 + azMS2 * azMS2).toDouble()).toFloat()

            if (totalAccMS2 > 15f) { // Próg uderzenia (powyżej ~1.5G)
                // dV = a * dt (gdzie dt = 10ms = 0.01s)
                deltaVelocityMS += totalAccMS2 * 0.010f
            }
        }
        return deltaVelocityMS * 3.6f // Konwersja z m/s na km/h
    }
    /**
     * Czas kontaktu stopy/rakiety z piłką (Contact Time) w milisekundach.
     */
    fun calculateContactTimeMs(hitSamples: List<SampleData>): Long {
        // Zliczamy próbki, w których przyspieszenie przekroczyło próg uderzenia
        val activeSamples = hitSamples.count {
            val aMag = sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()).toFloat()
            aMag > 2000f // Powyżej 2G (moment kontaktu)
        }
        return activeSamples * 10L // 1 próbka = 10 ms
    }

    /**
     * Wskaźnik czystości uderzenia (Smash Factor).
     * W sporcie to stosunek prędkości piłki do prędkości rakiety/stopy.
     * Bez czujnika na nodze wyliczamy go jako stosunek szczytowego G do czasu kontaktu (im wyższy pik w krótszym czasie, tym czystsze uderzenie).
     */
    fun calculateSmashFactor(hitSamples: List<SampleData>, peakValueMg: Float): Float {
        val contactTimeMs = calculateContactTimeMs(hitSamples)
        if (contactTimeMs == 0L) return 0f
        return (peakValueMg / 1000f) / contactTimeMs // G / ms
    }
    /**
     * Wylicza kierunek (kąt horyzontalny i wertykalny) wektora uderzenia w stopniach.
     */
    fun calculateImpactAngle(hitSamples: List<SampleData>): Pair<Float, Float> {
        if (hitSamples.isEmpty()) return Pair(0f, 0f)
        // Szukamy próbki z maksymalną siłą uderzenia
        val maxSample = hitSamples.maxByOrNull { sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()) } ?: hitSamples[0]

        // Kąt w płaszczyźnie poziomej (Azymut X-Y)
        val azimuthDeg = Math.toDegrees(atan2(maxSample.ay.toDouble(), maxSample.ax.toDouble())).toFloat()

        // Kąt w płaszczyźnie pionowej (Elewacja / Kąt podbicia)
        val totalHorizontal = sqrt((maxSample.ax * maxSample.ax + maxSample.ay * maxSample.ay).toDouble()).toFloat()
        val elevationDeg = Math.toDegrees(atan2(maxSample.az.toDouble(), totalHorizontal.toDouble())).toFloat()

        return Pair(azimuthDeg, elevationDeg)
    }
    /**
     * Całkowita energia kinetyczna przekazana piłce we wszystkich uderzeniach sesji (w Dżulach [J]).
     * Ek = 0.5 * m * v^2
     */
    fun calculateTotalEnergyExpenditureJ(history: List<HistoryEntry>): Float {
        var totalEnergy = 0f
        history.forEach { entry ->
            if (entry.type == EntryType.HIT) {
                val velocityKmH = calculateExitVelocityKmH(entry.samples)
                val velocityMS = velocityKmH / 3.6f
                totalEnergy += 0.5f * BALL_MASS_KG * velocityMS.pow(2)
            }
        }
        return totalEnergy
    }

    /**
     * Oblicza wskaźnik powtarzalności zawodnika (Consistency Score) w skali 0-100%.
     * Wyliczany na podstawie odchylenia standardowego siły uderzeń (im mniejsze odchylenie, tym wyższa powtarzalność).
     */
    fun calculateConsistencyScore(history: List<HistoryEntry>): Int {
        val hitPeaks = history.filter { it.type == EntryType.HIT }.map { it.peakValue }
        if (hitPeaks.size < 3) return 0 // Wymagane minimum 3 uderzenia do analizy stabilności

        val avg = hitPeaks.average().toFloat()
        val variance = hitPeaks.map { (it - avg).pow(2) }.sum() / hitPeaks.size
        val stdDeviation = sqrt(variance.toDouble()).toFloat()

        // Mapujemy odchylenie standardowe na procenty (np. odchylenie 5G to utrata 20% powtarzalności)
        val score = 100 - (stdDeviation / 1000f) * 4
        return score.coerceIn(0f, 100f).toInt()
    }
}