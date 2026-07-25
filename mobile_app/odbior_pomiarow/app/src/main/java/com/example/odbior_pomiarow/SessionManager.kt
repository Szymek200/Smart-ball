package com.example.odbior_pomiarow

import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider
import java.io.File
import java.io.FileWriter
import java.text.SimpleDateFormat
import java.util.*

object SessionManager {

    private const val SESSIONS_DIR_NAME = "SmartBall_Sessions"

    var currentSessionFolderName: String? = null
        private set

    private var sessionWriter: FileWriter? = null
    var isSessionActive = false
        private set

    /**
     * Zwraca główny katalog, w którym przechowywane są wszystkie sesje.
     */
    fun getSessionsBaseDir(context: Context): File {
        val baseDir = File(context.getExternalFilesDir(null), SESSIONS_DIR_NAME)
        if (!baseDir.exists()) baseDir.mkdirs()
        return baseDir
    }

    /**
     * Rozpoczyna nową sesję – tworzy folder i plik CSV na pomiary.
     */
    fun startNewSession(context: Context, sessionName: String): String {
        val timeStamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
        val folderName = "${sessionName.replace(" ", "_")}_$timeStamp"

        val sessionFolder = File(getSessionsBaseDir(context), folderName)
        if (!sessionFolder.exists()) sessionFolder.mkdirs()

        currentSessionFolderName = folderName
        isSessionActive = true

        // Tworzymy główny plik CSV sesji
        val csvFile = File(sessionFolder, "telemetry_data.csv")
        try {
            sessionWriter = FileWriter(csvFile, true)
            sessionWriter?.write("Timestamp,H3_AX,H3_AY,H3_AZ,IMU_AX,IMU_AY,IMU_AZ,IMU_GX,IMU_GY,IMU_GZ,Lat,Lon,Fix\n")
            sessionWriter?.flush()
        } catch (e: Exception) {
            e.printStackTrace()
        }

        return folderName
    }

    /**
     * Zapisuje pojedynczą próbkę do pliku CSV aktywnej sesji.
     */
    fun logSampleToCurrentSession(
        h3x: Float, h3y: Float, h3z: Float,
        ax: Float, ay: Float, az: Float,
        gx: Float, gy: Float, gz: Float,
        lat: Float, lon: Float, fix: Boolean
    ) {
        if (!isSessionActive || sessionWriter == null) return

        try {
            sessionWriter?.write("${System.currentTimeMillis()},$h3x,$h3y,$h3z,$ax,$ay,$az,$gx,$gy,$gz,$lat,$lon,${if (fix) 1 else 0}\n")
            sessionWriter?.flush()
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    /**
     * Kończy obecną sesję i zamyka strumień pliku.
     */
    fun stopCurrentSession() {
        try {
            sessionWriter?.flush()
            sessionWriter?.close()
        } catch (e: Exception) {
            e.printStackTrace()
        } finally {
            sessionWriter = null
            isSessionActive = false
            currentSessionFolderName = null
        }
    }

    /**
     * Udostępnia cały plik CSV z wybranej sesji za pomocą systemowego menu Androida.
     */
    fun shareSessionFile(context: Context, sessionFolderName: String) {
        val sessionFolder = File(getSessionsBaseDir(context), sessionFolderName)
        val csvFile = File(sessionFolder, "telemetry_data.csv")

        if (!csvFile.exists()) return

        val uri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.fileprovider",
            csvFile
        )

        val shareIntent = Intent(Intent.ACTION_SEND).apply {
            type = "text/csv"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }

        context.startActivity(Intent.createChooser(shareIntent, "Udostępnij trening CSV"))
    }
}