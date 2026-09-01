package com.example.epodd.utils

import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider
import java.io.File

object FileShareHandler {

    /**
     * Shares a converted .raw file via Android Share Sheet (Intent.ACTION_SEND)
     */
    fun shareFile(context: Context, file: File) {
        if (!file.exists()) return

        val uri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.provider",
            file
        )

        val shareIntent = Intent(Intent.ACTION_SEND).apply {
            type = "application/octet-stream"
            putExtra(Intent.EXTRA_STREAM, uri)
            putExtra(Intent.EXTRA_SUBJECT, "ePod Audio Track: ${file.name}")
            putExtra(Intent.EXTRA_TEXT, "Here is an ePod 22050Hz 8-bit unsigned PCM track ready for playback: ${file.name}")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }

        val chooser = Intent.createChooser(shareIntent, "Share ePod Track (${file.name})")
        chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(chooser)
    }
}
