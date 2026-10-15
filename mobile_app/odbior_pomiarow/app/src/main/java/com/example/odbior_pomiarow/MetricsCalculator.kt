package com.example.odbior_pomiarow

import android.content.Context
import android.content.SharedPreferences
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.pow
import kotlin.math.sqrt

object MetricsCalculator {

    // --- Parametry konfiguracyjne (zmienne) ---
    var ballMassKg: Float = 0.43f
    var hitThresholdG: Float = 3.5f // Domyślnie 3.5G, synchronizowany z parametrem HIT z BLE
    var airThresholdMg: Float = 300f // Próg wykrywania lotu (nieważkości)
    var minSpinThresholdDps: Float = 10.0f // Minimalny próg rotacji

    const val SAMPLE_TIME_S = 0.010f // 10 ms = 100 Hz
    const val GRAVITY_MS2 = 9.81f

    private const val PREFS_NAME = "SmartBall_Metrics_Prefs"
    private const val KEY_BALL_MASS = "key_ball_mass"
    private const val KEY_AIR_THS = "key_air_ths"
    private const val KEY_MIN_SPIN = "key_min_spin"

    /**
     * Wczytuje lokalne ustawienia kalkulatora z pamięci telefonu
     */
    fun loadPreferences(context: Context) {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        ballMassKg = prefs.getFloat(KEY_BALL_MASS, 0.43f)
        airThresholdMg = prefs.getFloat(KEY_AIR_THS, 300f)
        minSpinThresholdDps = prefs.getFloat(KEY_MIN_SPIN, 10.0f)
    }

    /**
     * Zapisuje parametry algorytmu zmienione w widoku Ustawień
     */
    fun savePreferences(context: Context, mass: Float, airThs: Float, minSpin: Float) {
        ballMassKg = mass
        airThresholdMg = airThs
        minSpinThresholdDps = minSpin

        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit().apply {
            putFloat(KEY_BALL_MASS, mass)
            putFloat(KEY_AIR_THS, airThs)
            putFloat(KEY_MIN_SPIN, minSpin)
            apply()
        }
    }

    /**
     * Aktualizuje próg uderzenia na podstawie wartości HIT_THS pobranej z ESP32 przez BLE
     */
    fun updateHitThresholdFromBle(hitThsG: Float) {
        hitThresholdG = hitThsG
    }

    fun calculateSpinRPM(sample: SampleData): Float {
        val magnitudeDps = sqrt((sample.gx * sample.gx + sample.gy * sample.gy + sample.gz * sample.gz).toDouble()).toFloat()
        return magnitudeDps / 6.0f
    }

    /**
     * Określa dominującą oś rotacji oraz jej charakterystykę fizyczną.
     * Zgodnie z orientacją: Z - pion, Y - oś w poprzek, X - oś wzdłużna.
     */
    fun determineSpinAxisAndMagnus(samples: List<SampleData>): String {
        if (samples.isEmpty()) return "Brak danych"

        val avgGx = samples.map { it.gx }.average().toFloat()
        val avgGy = samples.map { it.gy }.average().toFloat()
        val avgGz = samples.map { it.gz }.average().toFloat()

        val total = sqrt((avgGx * avgGx + avgGy * avgGy + avgGz * avgGz).toDouble()).toFloat()
        if (total < minSpinThresholdDps) return "Brak rotacji"

        // Gz - oś pionowa (Side-spin lewo/prawo)
        // Gy - oś poprzeczna (Top-spin / Back-spin)
        // Gx - oś wzdłużna lotu (Rifle-spin)
        return when {
            abs(avgGz) > abs(avgGy) && abs(avgGz) > abs(avgGx) -> if (avgGz > 0) "Side-spin (Prawo)" else "Side-spin (Lewo)"
            abs(avgGy) > abs(avgGz) && abs(avgGy) > abs(avgGx) -> if (avgGy > 0) "Top-spin (Opadająca)" else "Back-spin (Nośna)"
            else -> "Rifle-spin (Śrubowa)"
        }
    }

    fun calculateHangTimeSeconds(flightSamples: List<SampleData>): Float {
        val airSamplesCount = flightSamples.count {
            val aMag = sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()).toFloat()
            aMag < airThresholdMg // Konfigurowalny próg nieważkości
        }
        return airSamplesCount * SAMPLE_TIME_S
    }

    /**
     * Oblicza średnią wypadkową prędkość obrotową w stopniach na sekundę (dps).
     */
    fun calculateAverageRotationDps(samples: List<SampleData>): Float {
        if (samples.isEmpty()) return 0f
        return samples.map {
            sqrt((it.gx * it.gx + it.gy * it.gy + it.gz * it.gz).toDouble()).toFloat()
        }.average().toFloat()
    }

    /**
     * Oblicza średnią prędkość obrotową w obrotach na minutę (RPM).
     */
    fun calculateAverageRotationRPM(samples: List<SampleData>): Float {
        return calculateAverageRotationDps(samples) / 6.0f
    }

    fun calculateExitVelocityFromPacketKmH(eventSamples: List<SampleData>): Float {
        if (eventSamples.size < 5) return 0f

        val magnitudes = eventSamples.map { s ->
            sqrt((s.ax * s.ax + s.ay * s.ay + s.az * s.az).toDouble()).toFloat()
        }

        val maxPeakMg = magnitudes.maxOrNull() ?: 0f
        val peakIndex = magnitudes.indexOf(maxPeakMg)

        // Próg uderzenia z BLE przeliczony na mg (1G = 1000 mg)
        val hitPeakThresholdMg = hitThresholdG * 1000f
        if (maxPeakMg < hitPeakThresholdMg || peakIndex == -1) return 0f

        // Dolna granica zderzenia: 50% progu uderzenia lub min 1500 mg
        val collisionBoundaryMg = (hitPeakThresholdMg * 0.5f).coerceAtLeast(1500f)

        var hitStartIndex = peakIndex
        while (hitStartIndex > 0 && magnitudes[hitStartIndex - 1] > collisionBoundaryMg) {
            hitStartIndex--
        }
        var hitEndIndex = peakIndex
        while (hitEndIndex < eventSamples.size - 1 && magnitudes[hitEndIndex + 1] > collisionBoundaryMg) {
            hitEndIndex++
        }

        val preHitSamples = eventSamples.subList(0, hitStartIndex)
        val hitSamples = eventSamples.subList(hitStartIndex, hitEndIndex + 1)
        val postFlightSamples = eventSamples.subList(hitEndIndex + 1, eventSamples.size)

        // Kąt wznoszenia w momencie piku (oś Z pionowa)
        val peakSample = eventSamples[peakIndex]
        val horizontalMag = sqrt((peakSample.ax * peakSample.ax + peakSample.ay * peakSample.ay).toDouble()).toFloat()
        val elevationRad = atan2(peakSample.az.toDouble(), horizontalMag.toDouble()).toFloat()

        // Całkowanie wektorowe (grawitację odejmujemy tylko w pionie Z)
        var deltaVx = 0f
        var deltaVy = 0f
        var deltaVz = 0f

        hitSamples.forEach { s ->
            val axMS2 = (s.ax / 1000f) * GRAVITY_MS2
            val ayMS2 = (s.ay / 1000f) * GRAVITY_MS2
            val azMS2 = ((s.az / 1000f) * GRAVITY_MS2) - GRAVITY_MS2

            deltaVx += axMS2 * SAMPLE_TIME_S
            deltaVy += ayMS2 * SAMPLE_TIME_S
            deltaVz += azMS2 * SAMPLE_TIME_S
        }

        val deltaVelocityMS = sqrt((deltaVx * deltaVx + deltaVy * deltaVy + deltaVz * deltaVz).toDouble()).toFloat()

        var estimatedInitialSpeedMS = 0f
        if (preHitSamples.isNotEmpty()) {
            val wasInAir = preHitSamples.any {
                val aMag = sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()).toFloat()
                aMag < airThresholdMg
            }
            if (wasInAir) {
                estimatedInitialSpeedMS = 14f
            }
        }

        val exitSpeedMS = if (estimatedInitialSpeedMS > 0f) {
            val COR = 0.7f
            (deltaVelocityMS - ((1f - COR) * estimatedInitialSpeedMS)).coerceAtLeast(deltaVelocityMS * 0.6f)
        } else {
            deltaVelocityMS
        }

        var finalSpeedMS = exitSpeedMS
        if (postFlightSamples.isNotEmpty()) {
            val hangTimeS = calculateHangTimeSeconds(postFlightSamples)
            if (hangTimeS > 0.25f && elevationRad > 0.15f) {
                val vVertical = (GRAVITY_MS2 * hangTimeS) / 2f
                val sinElev = kotlin.math.sin(elevationRad.toDouble()).toFloat()
                if (sinElev > 0.1f) {
                    val ballisticSpeed = vVertical / sinElev
                    finalSpeedMS = maxOf(finalSpeedMS, ballisticSpeed)
                }
            }
        }

        return (finalSpeedMS * 3.6f).coerceIn(0f, 150f)
    }

    fun calculateContactTimeMs(hitSamples: List<SampleData>): Long {
        val boundaryMg = (hitThresholdG * 1000f * 0.5f).coerceAtLeast(1500f)
        val activeSamples = hitSamples.count {
            val aMag = sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()).toFloat()
            aMag > boundaryMg
        }
        return activeSamples * 10L
    }

    fun calculateSmashFactor(hitSamples: List<SampleData>, peakValueMg: Float): Float {
        val contactTimeMs = calculateContactTimeMs(hitSamples)
        if (contactTimeMs == 0L) return 0f
        return (peakValueMg / 1000f) / contactTimeMs
    }

    fun calculateImpactAngle(hitSamples: List<SampleData>): Pair<Float, Float> {
        if (hitSamples.isEmpty()) return Pair(0f, 0f)
        val maxSample = hitSamples.maxByOrNull { sqrt((it.ax * it.ax + it.ay * it.ay + it.az * it.az).toDouble()) } ?: hitSamples[0]
        val azimuthDeg = Math.toDegrees(atan2(maxSample.ay.toDouble(), maxSample.ax.toDouble())).toFloat()
        val totalHorizontal = sqrt((maxSample.ax * maxSample.ax + maxSample.ay * maxSample.ay).toDouble()).toFloat()
        val elevationDeg = Math.toDegrees(atan2(maxSample.az.toDouble(), totalHorizontal.toDouble())).toFloat()
        return Pair(azimuthDeg, elevationDeg)
    }

    fun calculateTotalEnergyExpenditureJ(history: List<HistoryEntry>): Float {
        var totalEnergy = 0f
        history.forEach { entry ->
            if (entry.type == EntryType.HIT) {
                val velocityKmH = calculateExitVelocityFromPacketKmH(entry.samples)
                val velocityMS = velocityKmH / 3.6f
                totalEnergy += 0.5f * ballMassKg * velocityMS.pow(2) // Użycie konfigurowalnej masy piłki
            }
        }
        return totalEnergy
    }

    fun calculateConsistencyScore(history: List<HistoryEntry>): Int {
        val hitPeaks = history.filter { it.type == EntryType.HIT }.map { it.peakValue }
        if (hitPeaks.size < 3) return 0

        val avg = hitPeaks.average().toFloat()
        val variance = hitPeaks.map { (it - avg).pow(2) }.sum() / hitPeaks.size
        val stdDeviation = sqrt(variance.toDouble()).toFloat()

        val score = 100 - (stdDeviation / 1000f) * 4
        return score.coerceIn(0f, 100f).toInt()
    }
}