package com.example.epodd.ui.navigation

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BatteryChargingFull
import androidx.compose.material.icons.filled.FolderShared
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Radar
import androidx.compose.ui.graphics.vector.ImageVector

sealed class Screen(val route: String, val title: String, val icon: ImageVector) {
    object Scan : Screen(route = "scan", title = "Scan", icon = Icons.Default.Radar)
    object Remote : Screen(route = "remote", title = "Playing", icon = Icons.Default.GraphicEq)
    object Transfer : Screen(route = "transfer", title = "Library", icon = Icons.Default.FolderShared)
    object Status : Screen(route = "status", title = "Status", icon = Icons.Default.BatteryChargingFull)

    companion object {
        val items: List<Screen>
            get() = listOf(Scan, Remote, Transfer, Status)
    }
}
