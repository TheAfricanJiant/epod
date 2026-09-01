package com.example.epodd.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.File
import java.io.FileInputStream

class AudioPreviewPlayer {

    companion object {
        private const val TAG = "AudioPreviewPlayer"
        private const val SAMPLE_RATE = 22050
    }

    private var audioTrack: AudioTrack? = null
    private var playbackJob: Job? = null

    private val _isPlaying = MutableStateFlow(false)
    val isPlaying: StateFlow<Boolean> = _isPlaying.asStateFlow()

    private val _activeTrackId = MutableStateFlow<String?>(null)
    val activeTrackId: StateFlow<String?> = _activeTrackId.asStateFlow()

    private val _playbackProgress = MutableStateFlow(0f)
    val playbackProgress: StateFlow<Float> = _playbackProgress.asStateFlow()

    fun playRawFile(trackId: String, rawFile: File) {
        if (!rawFile.exists() || rawFile.length() == 0L) return
        stop()

        _activeTrackId.value = trackId

        playbackJob = GlobalScope.launch(Dispatchers.IO) {
            val totalBytes = rawFile.length()
            val minBufferSize = AudioTrack.getMinBufferSize(
                SAMPLE_RATE,
                AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_8BIT
            )
            val bufferSize = maxOf(minBufferSize, 4096)

            val track = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_8BIT)
                        .setSampleRate(SAMPLE_RATE)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .build()
                )
                .setBufferSizeInBytes(bufferSize)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()

            audioTrack = track
            track.play()
            _isPlaying.value = true

            val buffer = ByteArray(bufferSize)
            var bytesRead = 0
            var totalRead = 0L

            try {
                FileInputStream(rawFile).use { fis ->
                    while (_isPlaying.value && fis.read(buffer).also { bytesRead = it } > 0) {
                        track.write(buffer, 0, bytesRead)
                        totalRead += bytesRead
                        _playbackProgress.value = (totalRead.toFloat() / totalBytes.toFloat()).coerceIn(0f, 1f)
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "AudioTrack playback error: ${e.localizedMessage}")
            } finally {
                stopPlaybackInternal()
            }
        }
    }

    fun stop() {
        _isPlaying.value = false
        playbackJob?.cancel()
        playbackJob = null
        stopPlaybackInternal()
    }

    private fun stopPlaybackInternal() {
        try {
            audioTrack?.let {
                if (it.playState == AudioTrack.PLAYSTATE_PLAYING) {
                    it.stop()
                }
                it.release()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error stopping AudioTrack: ${e.localizedMessage}")
        } finally {
            audioTrack = null
            _isPlaying.value = false
            _activeTrackId.value = null
            _playbackProgress.value = 0f
        }
    }
}
