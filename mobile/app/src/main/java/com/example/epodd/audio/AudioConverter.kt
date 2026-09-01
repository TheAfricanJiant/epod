package com.example.epodd.audio

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteOrder
import java.util.UUID
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

enum class DspMode {
    VOICE, // Default: High-pass 80Hz, De-ess -7dB @ 6.5kHz, Compression/Limiting 0.93, Low-pass 7.5kHz
    LOUD   // Instrumental: High-pass 80Hz, Peak limiting 0.93, full treble retention
}

data class ConvertedTrack(
    val id: String = UUID.randomUUID().toString(),
    val sanitizedFileName: String,
    val file: File,
    val durationSeconds: Int,
    val originalSizeBytes: Long,
    val outputSizeBytes: Long,
    val dspMode: DspMode,
    val timestamp: Long = System.currentTimeMillis()
)

class AudioConverter {

    companion object {
        private const val TAG = "AudioConverter"
        const val TARGET_SAMPLE_RATE = 22050
        const val TARGET_CHANNELS = 1
        const val SILENCE_BYTE: Byte = 0x80.toByte() // 128 unsigned
    }

    /**
     * Scans cache directory for previously saved converted .raw tracks so they persist across sessions
     */
    fun loadSavedConvertedTracks(context: Context): List<ConvertedTrack> {
        val cacheDir = File(context.cacheDir, "epod_raw")
        if (!cacheDir.exists()) return emptyList()

        val files = cacheDir.listFiles { _, name -> name.endsWith(".raw") } ?: return emptyList()
        return files.map { file ->
            val size = file.length()
            val durationSec = (size / TARGET_SAMPLE_RATE).toInt()
            ConvertedTrack(
                id = file.name,
                sanitizedFileName = file.name,
                file = file,
                durationSeconds = durationSec,
                originalSizeBytes = size,
                outputSizeBytes = size,
                dspMode = DspMode.VOICE,
                timestamp = file.lastModified()
            )
        }.sortedByDescending { it.timestamp }
    }

    /**
     * Deletes a converted .raw track from local cache storage
     */
    fun deleteConvertedTrack(track: ConvertedTrack): Boolean {
        return if (track.file.exists()) {
            track.file.delete()
        } else false
    }

    /**
     * Sanitizes track name according to ePod spec:
     * - Strips anything outside A-Z, a-z, 0-9, space, _, -
     * - Trims and truncates to max 32 characters
     * - Appends .raw extension
     */
    fun sanitizeFileName(inputName: String): String {
        val nameWithoutExt = inputName.substringBeforeLast(".")
        val cleaned = nameWithoutExt.replace(Regex("[^A-Za-z0-9 _-]"), "").trim()
        val truncated = if (cleaned.length > 32) cleaned.substring(0, 32).trim() else cleaned
        val finalName = if (truncated.isEmpty()) "track" else truncated
        return "$finalName.raw"
    }

    /**
     * Decodes source audio file (MP3, M4A, WAV, FLAC, AAC) using Android MediaCodec & MediaExtractor,
     * applies 8-bit DSP pipeline, resamples to 22050Hz mono unsigned 8-bit PCM, and clamps 0x00 -> 0x01.
     */
    suspend fun convertAudioFile(
        context: Context,
        inputUri: Uri,
        mode: DspMode = DspMode.VOICE,
        onProgress: (Float) -> Unit = {}
    ): ConvertedTrack = withContext(Dispatchers.IO) {

        var originalName = "audio.mp3"
        var originalSize = 0L

        context.contentResolver.query(inputUri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            val sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE)
            if (cursor.moveToFirst()) {
                if (nameIndex != -1) originalName = cursor.getString(nameIndex) ?: originalName
                if (sizeIndex != -1) originalSize = cursor.getLong(sizeIndex)
            }
        }

        val outputFileName = sanitizeFileName(originalName)
        val cacheDir = File(context.cacheDir, "epod_raw")
        if (!cacheDir.exists()) cacheDir.mkdirs()
        val outputFile = File(cacheDir, outputFileName)
        if (outputFile.exists()) outputFile.delete()

        val extractor = MediaExtractor()
        extractor.setDataSource(context, inputUri, null)

        var trackIndex = -1
        var inputFormat: MediaFormat? = null
        for (i in 0 until extractor.trackCount) {
            val format = extractor.getTrackFormat(i)
            val mime = format.getString(MediaFormat.KEY_MIME) ?: ""
            if (mime.startsWith("audio/")) {
                trackIndex = i
                inputFormat = format
                break
            }
        }

        if (trackIndex < 0 || inputFormat == null) {
            extractor.release()
            throw IllegalArgumentException("No audio track found in selected file: $originalName")
        }

        extractor.selectTrack(trackIndex)

        val mime = inputFormat.getString(MediaFormat.KEY_MIME)!!
        val inputSampleRate = inputFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        val inputChannelCount = inputFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        val durationUs = if (inputFormat.containsKey(MediaFormat.KEY_DURATION)) inputFormat.getLong(MediaFormat.KEY_DURATION) else 0L

        val decoder = MediaCodec.createDecoderByType(mime)
        decoder.configure(inputFormat, null, null, 0)
        decoder.start()

        val pcmSamples = mutableListOf<Short>()
        val bufferInfo = MediaCodec.BufferInfo()
        var isExtractorSawEOS = false
        var isDecoderSawEOS = false

        val timeoutUs = 10000L

        while (!isDecoderSawEOS) {
            if (!isExtractorSawEOS) {
                val inputBufIndex = decoder.dequeueInputBuffer(timeoutUs)
                if (inputBufIndex >= 0) {
                    val inputBuffer = decoder.getInputBuffer(inputBufIndex)
                    if (inputBuffer != null) {
                        val sampleSize = extractor.readSampleData(inputBuffer, 0)
                        if (sampleSize < 0) {
                            decoder.queueInputBuffer(inputBufIndex, 0, 0, 0L, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            isExtractorSawEOS = true
                        } else {
                            val sampleTimeUs = extractor.sampleTime
                            decoder.queueInputBuffer(inputBufIndex, 0, sampleSize, sampleTimeUs, 0)
                            extractor.advance()
                            if (durationUs > 0) {
                                val progress = (sampleTimeUs.toFloat() / durationUs.toFloat()).coerceIn(0f, 0.5f)
                                onProgress(progress)
                            }
                        }
                    }
                }
            }

            val outputBufIndex = decoder.dequeueOutputBuffer(bufferInfo, timeoutUs)
            if (outputBufIndex >= 0) {
                if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    isDecoderSawEOS = true
                }
                val outputBuffer = decoder.getOutputBuffer(outputBufIndex)
                if (outputBuffer != null && bufferInfo.size > 0) {
                    outputBuffer.position(bufferInfo.offset)
                    outputBuffer.limit(bufferInfo.offset + bufferInfo.size)
                    outputBuffer.order(ByteOrder.LITTLE_ENDIAN)

                    val shortBuffer = outputBuffer.asShortBuffer()
                    val count = shortBuffer.remaining()
                    val temp = ShortArray(count)
                    shortBuffer.get(temp)

                    if (inputChannelCount > 1) {
                        var i = 0
                        while (i < count) {
                            var sum = 0
                            for (ch in 0 until inputChannelCount) {
                                if (i + ch < count) sum += temp[i + ch]
                            }
                            pcmSamples.add((sum / inputChannelCount).toShort())
                            i += inputChannelCount
                        }
                    } else {
                        for (s in temp) pcmSamples.add(s)
                    }
                }
                decoder.releaseOutputBuffer(outputBufIndex, false)
            }
        }

        decoder.stop()
        decoder.release()
        extractor.release()

        onProgress(0.55f)

        // Resample to 22050 Hz
        val resampled = resamplePcm(pcmSamples.toShortArray(), inputSampleRate, TARGET_SAMPLE_RATE)
        onProgress(0.70f)

        // Apply DSP Filtering
        val dspProcessed = applyDspFilters(resampled, TARGET_SAMPLE_RATE, mode)
        onProgress(0.85f)

        // Quantize to 8-bit unsigned PCM [0x01..0xFF], clamp 0x00 -> 0x01
        val rawBytes = quantizeTo8BitUnsigned(dspProcessed)

        FileOutputStream(outputFile).use { fos ->
            fos.write(rawBytes)
        }

        onProgress(1.0f)

        val durationSec = (rawBytes.size / TARGET_SAMPLE_RATE)
        Log.i(TAG, "Converted: $outputFileName ($durationSec sec, ${rawBytes.size} bytes)")

        ConvertedTrack(
            sanitizedFileName = outputFileName,
            file = outputFile,
            durationSeconds = durationSec,
            originalSizeBytes = originalSize,
            outputSizeBytes = outputFile.length(),
            dspMode = mode
        )
    }

    private fun resamplePcm(input: ShortArray, fromRate: Int, toRate: Int): FloatArray {
        if (fromRate == toRate) {
            val out = FloatArray(input.size)
            for (i in input.indices) out[i] = input[i].toFloat() / 32768f
            return out
        }
        val ratio = fromRate.toDouble() / toRate.toDouble()
        val outputLen = (input.size / ratio).toInt()
        val output = FloatArray(outputLen)

        for (i in 0 until outputLen) {
            val srcPos = i * ratio
            val index = srcPos.toInt()
            val frac = (srcPos - index).toFloat()

            if (index + 1 < input.size) {
                val s1 = input[index].toFloat() / 32768f
                val s2 = input[index + 1].toFloat() / 32768f
                output[i] = s1 + frac * (s2 - s1)
            } else if (index < input.size) {
                output[i] = input[index].toFloat() / 32768f
            }
        }
        return output
    }

    private fun applyDspFilters(samples: FloatArray, sampleRate: Int, mode: DspMode): FloatArray {
        var buffer = samples.copyOf()
        buffer = applyHighPass(buffer, 80f, sampleRate)

        if (mode == DspMode.VOICE) {
            buffer = applyEqualizer(buffer, 6500f, -7f, sampleRate)
            buffer = applyLowPass(buffer, 7500f, sampleRate)
        }

        var maxPeak = 0.0001f
        for (s in buffer) {
            val absS = abs(s)
            if (absS > maxPeak) maxPeak = absS
        }

        val targetPeak = 0.93f
        val gain = min(targetPeak / maxPeak, 8.0f)

        for (i in buffer.indices) {
            var s = buffer[i] * gain
            if (s > 0.93f) s = 0.93f
            if (s < -0.93f) s = -0.93f
            buffer[i] = s
        }

        return buffer
    }

    private fun applyHighPass(samples: FloatArray, cutoffHz: Float, sampleRate: Int): FloatArray {
        val rc = 1.0f / (2.0f * Math.PI.toFloat() * cutoffHz)
        val dt = 1.0f / sampleRate
        val alpha = rc / (rc + dt)

        val out = FloatArray(samples.size)
        out[0] = samples[0]
        for (i in 1 until samples.size) {
            out[i] = alpha * (out[i - 1] + samples[i] - samples[i - 1])
        }
        return out
    }

    private fun applyLowPass(samples: FloatArray, cutoffHz: Float, sampleRate: Int): FloatArray {
        val rc = 1.0f / (2.0f * Math.PI.toFloat() * cutoffHz)
        val dt = 1.0f / sampleRate
        val alpha = dt / (rc + dt)

        val out = FloatArray(samples.size)
        out[0] = samples[0]
        for (i in 1 until samples.size) {
            out[i] = out[i - 1] + alpha * (samples[i] - out[i - 1])
        }
        return out
    }

    private fun applyEqualizer(samples: FloatArray, centerFreqHz: Float, gainDb: Float, sampleRate: Int): FloatArray {
        val out = FloatArray(samples.size)
        val w0 = (2.0 * Math.PI * centerFreqHz / sampleRate).toFloat()
        val alpha = (sin(w0.toDouble()) / 2.0).toFloat()
        val A = Math.pow(10.0, gainDb / 40.0).toFloat()

        val b0 = 1.0f + alpha * A
        val b1 = -2.0f * cos(w0)
        val b2 = 1.0f - alpha * A
        val a0 = 1.0f + alpha / A
        val a1 = -2.0f * cos(w0)
        val a2 = 1.0f - alpha / A

        var x1 = 0f
        var x2 = 0f
        var y1 = 0f
        var y2 = 0f

        for (i in samples.indices) {
            val x0 = samples[i]
            val y0 = (b0 / a0) * x0 + (b1 / a0) * x1 + (b2 / a0) * x2 - (a1 / a0) * y1 - (a2 / a0) * y2
            out[i] = y0
            x2 = x1
            x1 = x0
            y2 = y1
            y1 = y0
        }
        return out
    }

    private fun quantizeTo8BitUnsigned(samples: FloatArray): ByteArray {
        val bytes = ByteArray(samples.size)
        for (i in samples.indices) {
            val normalized = ((samples[i] + 1.0f) * 127.5f).toInt()
            var byteVal = normalized.coerceIn(1, 255)
            if (byteVal == 0) byteVal = 1
            bytes[i] = byteVal.toByte()
        }
        return bytes
    }
}
