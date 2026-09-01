package com.example.epodd.ui.screens

import android.app.Application
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AudioFile
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Radar
import androidx.compose.material.icons.filled.SdCard
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.filled.Transform
import androidx.compose.material.icons.filled.UploadFile
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CheckboxDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.example.epodd.audio.ConvertedTrack
import com.example.epodd.audio.DspMode
import com.example.epodd.ble.ConnectionState
import com.example.epodd.ble.DiscoveredDevice
import com.example.epodd.ui.theme.BatteryAmber
import com.example.epodd.ui.theme.CircuitGreen
import com.example.epodd.ui.theme.CopperHardware
import com.example.epodd.ui.theme.GradientGreenEnd
import com.example.epodd.ui.theme.GradientGreenStart
import com.example.epodd.ui.theme.StatusWarnAmber
import com.example.epodd.viewmodel.EPodViewModel
import com.example.epodd.viewmodel.TransportMode

@Composable
fun TransferScreen(viewModel: EPodViewModel? = null) {
    val context = LocalContext.current
    val appContext = context.applicationContext

    // ── ViewModel State ──
    val pendingUris by viewModel?.pendingUris?.collectAsState() ?: remember { mutableStateOf(emptyList()) }
    val dspMode by viewModel?.dspMode?.collectAsState() ?: remember { mutableStateOf(DspMode.VOICE) }

    val isConverting by viewModel?.isConverting?.collectAsState() ?: remember { mutableStateOf(false) }
    val batchConversionProgress by viewModel?.batchConversionProgress?.collectAsState() ?: remember { mutableFloatStateOf(0f) }
    val currentConvertingFileName by viewModel?.currentConvertingFileName?.collectAsState() ?: remember { mutableStateOf("") }
    val currentConvertingIndex by viewModel?.currentConvertingIndex?.collectAsState() ?: remember { mutableStateOf(0) }
    val totalConvertingCount by viewModel?.totalConvertingCount?.collectAsState() ?: remember { mutableStateOf(0) }

    val convertedTracks by viewModel?.convertedTracks?.collectAsState() ?: remember { mutableStateOf(emptyList()) }
    val selectedTrackIds by viewModel?.selectedTrackIds?.collectAsState() ?: remember { mutableStateOf(emptySet()) }

    val isPreviewPlaying by viewModel?.isPreviewPlaying?.collectAsState() ?: remember { mutableStateOf(false) }
    val activePreviewTrackId by viewModel?.activePreviewTrackId?.collectAsState() ?: remember { mutableStateOf(null) }
    val previewProgress by viewModel?.previewProgress?.collectAsState() ?: remember { mutableFloatStateOf(0f) }

    val transportMode by viewModel?.transportMode?.collectAsState() ?: remember { mutableStateOf(TransportMode.WIFI_HTTP) }
    val wifiHostIp by viewModel?.wifiHostIp?.collectAsState() ?: remember { mutableStateOf("192.168.4.1") }
    val isDeviceScannerOpen by viewModel?.isDeviceScannerOpen?.collectAsState() ?: remember { mutableStateOf(false) }

    val transferProgress by viewModel?.transferProgress?.collectAsState() ?: remember { mutableFloatStateOf(0f) }
    val transferStatus by viewModel?.transferStatus?.collectAsState() ?: remember { mutableStateOf("Ready") }
    val isTransferring by viewModel?.isTransferring?.collectAsState() ?: remember { mutableStateOf(false) }
    val connectionState by viewModel?.connectionState?.collectAsState() ?: remember { mutableStateOf<ConnectionState>(ConnectionState.Disconnected) }
    val discoveredDevices by viewModel?.discoveredDevices?.collectAsState() ?: remember { mutableStateOf(emptyList()) }

    val isConnected = connectionState is ConnectionState.Connected

    val animatedConvProgress by animateFloatAsState(
        targetValue = batchConversionProgress,
        animationSpec = tween(durationMillis = 300, easing = FastOutSlowInEasing),
        label = "ConvProgressAnim"
    )
    val animatedTransferProgress by animateFloatAsState(
        targetValue = transferProgress,
        animationSpec = tween(durationMillis = 300, easing = FastOutSlowInEasing),
        label = "TransferProgressAnim"
    )

    // Multi-file audio picker
    val multiFilePickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetMultipleContents()
    ) { uris ->
        if (uris.isNotEmpty()) viewModel?.onMultipleFilesSelected(uris)
    }

    // ── Root Box for overlay support ──
    Box(modifier = Modifier.fillMaxSize()) {

        // ── Main scrollable content ──
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
                .padding(horizontal = 20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            // Header
            item {
                Column(modifier = Modifier.padding(top = 16.dp, bottom = 4.dp)) {
                    Text(
                        text = "Audio Converter & Library",
                        style = MaterialTheme.typography.headlineLarge,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = "Pick audio files → convert to ePod .raw → test & send",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
            }

            // ─── Section 1: File Picker & Converter ───
            item {
                Card(
                    shape = MaterialTheme.shapes.large,
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
                    modifier = Modifier
                        .fillMaxWidth()
                        .border(
                            1.dp,
                            MaterialTheme.colorScheme.outline.copy(alpha = 0.2f),
                            MaterialTheme.shapes.large
                        )
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        // Row: title + pick button
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f).padding(end = 8.dp)) {
                                Text(
                                    text = "1. Select Audio Files",
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                                Text(
                                    text = if (pendingUris.isNotEmpty())
                                        "${pendingUris.size} file(s) queued for conversion"
                                    else
                                        "MP3, M4A, WAV, FLAC, AAC supported",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = if (pendingUris.isNotEmpty()) CircuitGreen
                                    else MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }

                            Button(
                                onClick = { multiFilePickerLauncher.launch("audio/*") },
                                enabled = !isConverting && !isTransferring,
                                shape = MaterialTheme.shapes.medium,
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = MaterialTheme.colorScheme.primary
                                )
                            ) {
                                Icon(
                                    imageVector = Icons.Default.UploadFile,
                                    contentDescription = "Pick Files",
                                    modifier = Modifier.size(18.dp)
                                )
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(
                                    "Pick Audio",
                                    style = MaterialTheme.typography.labelMedium,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                        }

                        if (pendingUris.isNotEmpty()) {
                            Spacer(modifier = Modifier.height(12.dp))

                            // DSP Mode Chips
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                FilterChip(
                                    selected = dspMode == DspMode.VOICE,
                                    onClick = { viewModel?.setDspMode(DspMode.VOICE) },
                                    enabled = !isConverting,
                                    label = { Text("Voice (De-ess)", fontSize = 12.sp) },
                                    leadingIcon = {
                                        Icon(
                                            imageVector = Icons.Default.GraphicEq,
                                            contentDescription = null,
                                            modifier = Modifier.size(14.dp)
                                        )
                                    },
                                    colors = FilterChipDefaults.filterChipColors(
                                        selectedContainerColor = CircuitGreen.copy(alpha = 0.2f),
                                        selectedLabelColor = CircuitGreen
                                    )
                                )
                                FilterChip(
                                    selected = dspMode == DspMode.LOUD,
                                    onClick = { viewModel?.setDspMode(DspMode.LOUD) },
                                    enabled = !isConverting,
                                    label = { Text("Loud (Treble)", fontSize = 12.sp) },
                                    leadingIcon = {
                                        Icon(
                                            imageVector = Icons.Default.GraphicEq,
                                            contentDescription = null,
                                            modifier = Modifier.size(14.dp)
                                        )
                                    },
                                    colors = FilterChipDefaults.filterChipColors(
                                        selectedContainerColor = CopperHardware.copy(alpha = 0.2f),
                                        selectedLabelColor = CopperHardware
                                    )
                                )
                            }

                            Spacer(modifier = Modifier.height(12.dp))

                            // Active conversion progress OR convert button
                            if (isConverting) {
                                ConversionProgressBar(
                                    animatedProgress = animatedConvProgress,
                                    currentFileName = currentConvertingFileName,
                                    currentIndex = currentConvertingIndex,
                                    totalCount = totalConvertingCount
                                )
                            } else {
                                Button(
                                    onClick = { viewModel?.startBatchConversion(appContext) },
                                    modifier = Modifier.fillMaxWidth(),
                                    shape = MaterialTheme.shapes.medium,
                                    colors = ButtonDefaults.buttonColors(containerColor = CircuitGreen)
                                ) {
                                    Icon(
                                        imageVector = Icons.Default.Transform,
                                        contentDescription = "Convert",
                                        modifier = Modifier.size(18.dp)
                                    )
                                    Spacer(modifier = Modifier.width(8.dp))
                                    Text(
                                        "Convert ${pendingUris.size} File(s) to .raw",
                                        style = MaterialTheme.typography.titleSmall,
                                        fontWeight = FontWeight.Bold
                                    )
                                }

                                TextButton(
                                    onClick = { viewModel?.clearPendingFiles() },
                                    modifier = Modifier.align(Alignment.End)
                                ) {
                                    Text(
                                        "Clear Queue",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }
                    }
                }
            }

            // ─── Section 2: Converted Track Library header ───
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.SdCard,
                            contentDescription = "Library",
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = "Track Library (${convertedTracks.size})",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }

                    if (convertedTracks.isNotEmpty()) {
                        Row {
                            TextButton(onClick = { viewModel?.selectAllTracks() }) {
                                Text(
                                    "All",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = CircuitGreen
                                )
                            }
                            TextButton(onClick = { viewModel?.clearTrackSelection() }) {
                                Text(
                                    "None",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                }
            }

            // ─── Section 2: Track list ───
            if (convertedTracks.isEmpty()) {
                item {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(110.dp)
                            .clip(MaterialTheme.shapes.medium)
                            .background(MaterialTheme.colorScheme.surface)
                            .border(
                                1.dp,
                                MaterialTheme.colorScheme.outline.copy(alpha = 0.15f),
                                MaterialTheme.shapes.medium
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(
                                imageVector = Icons.Default.MusicNote,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f),
                                modifier = Modifier.size(28.dp)
                            )
                            Spacer(modifier = Modifier.height(6.dp))
                            Text(
                                text = "No converted tracks yet",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                textAlign = TextAlign.Center
                            )
                            Text(
                                text = "Pick audio files above and tap Convert",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                                textAlign = TextAlign.Center
                            )
                        }
                    }
                }
            } else {
                items(convertedTracks, key = { it.id }) { track ->
                    ConvertedTrackCardItem(
                        track = track,
                        isSelected = selectedTrackIds.contains(track.id),
                        isPreviewPlaying = isPreviewPlaying && activePreviewTrackId == track.id,
                        previewProgress = if (activePreviewTrackId == track.id) previewProgress else 0f,
                        onToggleSelect = { viewModel?.toggleTrackSelection(track.id) },
                        onTogglePreview = { viewModel?.toggleTrackPreview(track) },
                        onShare = { viewModel?.shareTrack(context, track) },
                        onDelete = { viewModel?.deleteTrack(track) }
                    )
                }
            }

            // ─── Section 3: Transport & Send ───
            item {
                SendToEpodCard(
                    selectedCount = selectedTrackIds.size,
                    totalCount = convertedTracks.size,
                    transportMode = transportMode,
                    wifiHostIp = wifiHostIp,
                    isConnected = isConnected,
                    isTransferring = isTransferring,
                    transferStatus = transferStatus,
                    animatedTransferProgress = animatedTransferProgress,
                    onSelectTransport = { viewModel?.setTransportMode(it) },
                    onIpChanged = { viewModel?.setWifiHostIp(it) },
                    onSend = { viewModel?.sendSelectedTracks(context) },
                    onCancel = { viewModel?.cancelFileTransfer() }
                )
            }

            // Bottom spacing
            item { Spacer(modifier = Modifier.height(24.dp)) }
        }

        // ─── BLE Device Scanner Modal overlay ───
        if (isDeviceScannerOpen) {
            Dialog(
                onDismissRequest = { viewModel?.closeDeviceScanner() },
                properties = DialogProperties(usePlatformDefaultWidth = false)
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(Color.Black.copy(alpha = 0.55f))
                        .clickable { viewModel?.closeDeviceScanner() },
                    contentAlignment = Alignment.Center
                ) {
                    Card(
                        shape = RoundedCornerShape(24.dp),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.surface
                        ),
                        modifier = Modifier
                            .padding(24.dp)
                            .fillMaxWidth()
                            .clickable(enabled = false) {} // block dismiss on card tap
                            .border(2.dp, CircuitGreen, RoundedCornerShape(24.dp))
                    ) {
                        Column(modifier = Modifier.padding(20.dp)) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    val infiniteTransition =
                                        rememberInfiniteTransition(label = "ScannerPulse")
                                    val scanPulse by infiniteTransition.animateFloat(
                                        initialValue = 0.85f,
                                        targetValue = 1.15f,
                                        animationSpec = infiniteRepeatable(
                                            animation = tween(900, easing = FastOutSlowInEasing),
                                            repeatMode = RepeatMode.Reverse
                                        ),
                                        label = "ScanPulse"
                                    )
                                    Icon(
                                        imageVector = Icons.Default.Radar,
                                        contentDescription = "Scanning",
                                        tint = CircuitGreen,
                                        modifier = Modifier.size(22.dp).scale(scanPulse)
                                    )
                                    Spacer(modifier = Modifier.width(10.dp))
                                    Column {
                                        Text(
                                            "Connect to ePod",
                                            style = MaterialTheme.typography.titleMedium,
                                            fontWeight = FontWeight.Bold,
                                            color = MaterialTheme.colorScheme.onSurface
                                        )
                                        Text(
                                            "Your converted tracks are safe",
                                            style = MaterialTheme.typography.labelSmall,
                                            color = CircuitGreen
                                        )
                                    }
                                }
                                IconButton(onClick = { viewModel?.closeDeviceScanner() }) {
                                    Icon(
                                        imageVector = Icons.Default.Close,
                                        contentDescription = "Close scanner"
                                    )
                                }
                            }

                            Spacer(modifier = Modifier.height(16.dp))

                            if (discoveredDevices.isEmpty()) {
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .height(80.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    Text(
                                        "Scanning for ePod devices nearby…",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        textAlign = TextAlign.Center
                                    )
                                }
                            } else {
                                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                    discoveredDevices.forEach { dev ->
                                        BleDeviceRow(
                                            device = dev,
                                            onConnect = { viewModel?.connectToDevice(dev.device) }
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ─── Conversion Progress Component ───

@Composable
private fun ConversionProgressBar(
    animatedProgress: Float,
    currentFileName: String,
    currentIndex: Int,
    totalCount: Int
) {
    val infiniteTransition = rememberInfiniteTransition(label = "ConvPulse")
    val dotAlpha by infiniteTransition.animateFloat(
        initialValue = 0.4f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(700),
            repeatMode = RepeatMode.Reverse
        ),
        label = "DotAlpha"
    )

    Column {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.weight(1f)
            ) {
                Box(
                    modifier = Modifier
                        .size(7.dp)
                        .clip(CircleShape)
                        .background(CircuitGreen.copy(alpha = dotAlpha))
                )
                Spacer(modifier = Modifier.width(6.dp))
                Text(
                    text = if (currentFileName.isNotEmpty())
                        "Converting: ${currentFileName.substringBeforeLast(".")}"
                    else "Preparing…",
                    style = MaterialTheme.typography.labelSmall,
                    color = CircuitGreen,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Text(
                text = if (totalCount > 0) "$currentIndex / $totalCount  •  ${(animatedProgress * 100).toInt()}%"
                else "${(animatedProgress * 100).toInt()}%",
                style = MaterialTheme.typography.labelSmall,
                fontFamily = FontFamily.Monospace,
                color = CircuitGreen
            )
        }
        Spacer(modifier = Modifier.height(6.dp))
        LinearProgressIndicator(
            progress = { animatedProgress },
            modifier = Modifier
                .fillMaxWidth()
                .height(8.dp)
                .clip(CircleShape),
            color = CircuitGreen,
            trackColor = CircuitGreen.copy(alpha = 0.15f)
        )
    }
}

// ─── Converted Track Card ───

@Composable
fun ConvertedTrackCardItem(
    track: ConvertedTrack,
    isSelected: Boolean,
    isPreviewPlaying: Boolean,
    previewProgress: Float,
    onToggleSelect: () -> Unit,
    onTogglePreview: () -> Unit,
    onShare: () -> Unit,
    onDelete: () -> Unit
) {
    val cardBorder = if (isSelected) CircuitGreen.copy(alpha = 0.7f)
    else MaterialTheme.colorScheme.outline.copy(alpha = 0.15f)

    Card(
        shape = MaterialTheme.shapes.medium,
        colors = CardDefaults.cardColors(
            containerColor = if (isSelected)
                CircuitGreen.copy(alpha = 0.06f)
            else MaterialTheme.colorScheme.surface
        ),
        modifier = Modifier
            .fillMaxWidth()
            .border(1.dp, cardBorder, MaterialTheme.shapes.medium)
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                // Checkbox + file info
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.weight(1f)
                ) {
                    Checkbox(
                        checked = isSelected,
                        onCheckedChange = { onToggleSelect() },
                        colors = CheckboxDefaults.colors(checkedColor = CircuitGreen)
                    )
                    Spacer(modifier = Modifier.width(4.dp))

                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = track.sanitizedFileName.substringBeforeLast("."),
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = "${track.durationSeconds}s  •  ${track.outputSizeBytes / 1024} KB  •  22050 Hz 8-bit",
                            style = MaterialTheme.typography.labelSmall,
                            fontFamily = FontFamily.Monospace,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }

                // Actions: Play/Pause • Share • Delete
                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(
                        onClick = onTogglePreview,
                        modifier = Modifier
                            .size(36.dp)
                            .clip(CircleShape)
                            .background(
                                if (isPreviewPlaying) CircuitGreen
                                else MaterialTheme.colorScheme.surfaceVariant
                            )
                    ) {
                        Icon(
                            imageVector = if (isPreviewPlaying) Icons.Default.Pause else Icons.Default.PlayArrow,
                            contentDescription = if (isPreviewPlaying) "Stop preview" else "Test audio",
                            tint = if (isPreviewPlaying) MaterialTheme.colorScheme.surface else CircuitGreen,
                            modifier = Modifier.size(20.dp)
                        )
                    }

                    IconButton(onClick = onShare) {
                        Icon(
                            imageVector = Icons.Default.Share,
                            contentDescription = "Share .raw file",
                            tint = MaterialTheme.colorScheme.secondary,
                            modifier = Modifier.size(18.dp)
                        )
                    }

                    IconButton(onClick = onDelete) {
                        Icon(
                            imageVector = Icons.Default.Delete,
                            contentDescription = "Delete track",
                            tint = StatusWarnAmber,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                }
            }

            // Audio preview progress bar (shown only when this track is playing)
            AnimatedVisibility(visible = isPreviewPlaying) {
                Column {
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "▶ TESTING",
                            style = MaterialTheme.typography.labelSmall,
                            fontFamily = FontFamily.Monospace,
                            color = CircuitGreen,
                            fontSize = 10.sp
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        LinearProgressIndicator(
                            progress = { previewProgress },
                            modifier = Modifier
                                .weight(1f)
                                .height(4.dp)
                                .clip(CircleShape),
                            color = CircuitGreen,
                            trackColor = CircuitGreen.copy(alpha = 0.15f)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = "${(previewProgress * 100).toInt()}%",
                            style = MaterialTheme.typography.labelSmall,
                            fontFamily = FontFamily.Monospace,
                            color = CircuitGreen,
                            fontSize = 10.sp
                        )
                    }
                }
            }
        }
    }
}

// ─── Send To ePod Card ───

@Composable
private fun SendToEpodCard(
    selectedCount: Int,
    totalCount: Int,
    transportMode: TransportMode,
    wifiHostIp: String,
    isConnected: Boolean,
    isTransferring: Boolean,
    transferStatus: String,
    animatedTransferProgress: Float,
    onSelectTransport: (TransportMode) -> Unit,
    onIpChanged: (String) -> Unit,
    onSend: () -> Unit,
    onCancel: () -> Unit
) {
    Card(
        shape = MaterialTheme.shapes.large,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        modifier = Modifier
            .fillMaxWidth()
            .border(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.25f), MaterialTheme.shapes.large)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {

            // Transport selector row
            Text(
                "3. Transfer to ePod",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurface
            )
            Spacer(modifier = Modifier.height(10.dp))

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(
                    selected = transportMode == TransportMode.WIFI_HTTP,
                    onClick = { onSelectTransport(TransportMode.WIFI_HTTP) },
                    label = { Text("Wi-Fi (Fast)", fontSize = 12.sp) },
                    leadingIcon = {
                        Icon(
                            imageVector = Icons.Default.Wifi,
                            contentDescription = null,
                            modifier = Modifier.size(14.dp)
                        )
                    },
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = CircuitGreen.copy(alpha = 0.2f),
                        selectedLabelColor = CircuitGreen
                    )
                )
                FilterChip(
                    selected = transportMode == TransportMode.BLE_GATT,
                    onClick = { onSelectTransport(TransportMode.BLE_GATT) },
                    label = { Text("BLE Stream", fontSize = 12.sp) },
                    leadingIcon = {
                        Icon(
                            imageVector = Icons.Default.Bluetooth,
                            contentDescription = null,
                            modifier = Modifier.size(14.dp)
                        )
                    },
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = MaterialTheme.colorScheme.primaryContainer,
                        selectedLabelColor = MaterialTheme.colorScheme.primary
                    )
                )
            }

            // Wi-Fi IP field
            if (transportMode == TransportMode.WIFI_HTTP) {
                Spacer(modifier = Modifier.height(8.dp))
                OutlinedTextField(
                    value = wifiHostIp,
                    onValueChange = onIpChanged,
                    label = { Text("ePod Soft-AP IP", fontSize = 11.sp) },
                    placeholder = { Text("192.168.4.1", fontSize = 12.sp, fontFamily = FontFamily.Monospace) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    textStyle = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                    colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = CircuitGreen),
                    supportingText = {
                        Text(
                            "Connect your phone to ePod's Wi-Fi network first",
                            fontSize = 10.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                        )
                    }
                )
            }

            // BLE connection status hint
            if (transportMode == TransportMode.BLE_GATT) {
                Spacer(modifier = Modifier.height(6.dp))
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(
                            if (isConnected) CircuitGreen.copy(alpha = 0.1f)
                            else StatusWarnAmber.copy(alpha = 0.1f)
                        )
                        .padding(horizontal = 10.dp, vertical = 6.dp)
                ) {
                    Icon(
                        imageVector = if (isConnected) Icons.Default.Check else Icons.Default.Bluetooth,
                        contentDescription = null,
                        tint = if (isConnected) CircuitGreen else StatusWarnAmber,
                        modifier = Modifier.size(14.dp)
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = if (isConnected) "ePod BLE Connected" else "No BLE connection — tapping Send will open scanner",
                        style = MaterialTheme.typography.labelSmall,
                        color = if (isConnected) CircuitGreen else StatusWarnAmber
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Transfer progress OR send button
            if (isTransferring) {
                Column {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = transferStatus,
                            style = MaterialTheme.typography.labelSmall,
                            color = CircuitGreen,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.weight(1f)
                        )
                        Text(
                            text = "${(animatedTransferProgress * 100).toInt()}%",
                            style = MaterialTheme.typography.labelSmall,
                            fontFamily = FontFamily.Monospace,
                            color = CircuitGreen
                        )
                    }
                    Spacer(modifier = Modifier.height(6.dp))
                    LinearProgressIndicator(
                        progress = { animatedTransferProgress },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(8.dp)
                            .clip(CircleShape),
                        color = CircuitGreen,
                        trackColor = CircuitGreen.copy(alpha = 0.15f)
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    OutlinedButton(
                        onClick = onCancel,
                        modifier = Modifier.fillMaxWidth(),
                        shape = MaterialTheme.shapes.small
                    ) {
                        Text("Cancel Upload", style = MaterialTheme.typography.labelMedium, color = StatusWarnAmber)
                    }
                }
            } else {
                val hasSelection = selectedCount > 0
                Button(
                    onClick = onSend,
                    enabled = hasSelection,
                    modifier = Modifier.fillMaxWidth(),
                    shape = MaterialTheme.shapes.medium,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary,
                        disabledContainerColor = MaterialTheme.colorScheme.surfaceVariant
                    )
                ) {
                    Icon(
                        imageVector = Icons.Default.SdCard,
                        contentDescription = "Send",
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = when {
                            !hasSelection && totalCount == 0 -> "Convert tracks first"
                            !hasSelection -> "Select tracks to send (checkbox)"
                            transportMode == TransportMode.BLE_GATT && !isConnected ->
                                "Send $selectedCount Track(s) via BLE →"
                            else -> "Send $selectedCount Track(s) to ePod"
                        },
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold
                    )
                }

                if (!hasSelection && totalCount > 0) {
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = "Tip: Check the boxes next to tracks you want to send",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                        textAlign = TextAlign.Center,
                        modifier = Modifier.fillMaxWidth()
                    )
                }
            }
        }
    }
}

// ─── BLE Device Row (inside scanner modal) ───

@Composable
private fun BleDeviceRow(device: DiscoveredDevice, onConnect: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(MaterialTheme.shapes.small)
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f))
            .clickable { onConnect() }
            .padding(horizontal = 14.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.weight(1f)) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(CircleShape)
                    .background(
                        Brush.radialGradient(
                            colors = listOf(
                                GradientGreenStart.copy(alpha = 0.25f),
                                GradientGreenEnd.copy(alpha = 0.1f)
                            )
                        )
                    ),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Default.Bluetooth,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(18.dp)
                )
            }
            Spacer(modifier = Modifier.width(10.dp))
            Column {
                Text(
                    device.name,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    "${device.address}  •  ${device.rssi} dBm",
                    style = MaterialTheme.typography.labelSmall,
                    fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        Button(
            onClick = onConnect,
            shape = MaterialTheme.shapes.small,
            colors = ButtonDefaults.buttonColors(containerColor = CircuitGreen),
            modifier = Modifier.padding(start = 8.dp)
        ) {
            Text("Connect", style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Bold)
        }
    }
}
