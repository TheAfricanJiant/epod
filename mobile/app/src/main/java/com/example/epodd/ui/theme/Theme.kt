package com.example.epodd.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

// Stitch Elite Dark Color Scheme (Lithium Dark Slate with Circuit Green)
private val EPodDarkColorScheme = darkColorScheme(
    primary = CircuitGreen,
    onPrimary = SurfaceDark,
    primaryContainer = ForestGreenPrimary,
    onPrimaryContainer = ForestGreenLight,
    secondary = RecycledSlate,
    onSecondary = OnSurfaceDark,
    secondaryContainer = SurfaceContainerDark,
    onSecondaryContainer = ForestGreenLight,
    tertiary = StatusWarnAmber,
    onTertiary = SurfaceDark,
    background = SurfaceDark,
    onBackground = OnSurfaceDark,
    surface = SurfaceContainerDark,
    onSurface = OnSurfaceDark,
    surfaceVariant = SurfaceVariantDark,
    onSurfaceVariant = OnSurfaceVariantDark,
    outline = OutlineDark,
    outlineVariant = OutlineVariantDark
)

// Stitch Elite Light Color Scheme (Sage Executive Matte Bone & Forest Green)
private val EPodLightColorScheme = lightColorScheme(
    primary = ForestGreenPrimary,
    onPrimary = SurfaceContainerLowestLight,
    primaryContainer = ForestGreenContainer,
    onPrimaryContainer = OnForestGreenContainer,
    secondary = RecycledSlate,
    onSecondary = SurfaceContainerLowestLight,
    secondaryContainer = SurfaceContainerLowLight,
    onSecondaryContainer = OnSurfaceVariantLight,
    tertiary = StatusWarnAmber,
    onTertiary = SurfaceContainerLowestLight,
    background = SurfaceLight,
    onBackground = OnSurfaceLight,
    surface = SurfaceContainerLowestLight,
    onSurface = OnSurfaceLight,
    surfaceVariant = SurfaceVariantLight,
    onSurfaceVariant = OnSurfaceVariantLight,
    outline = OutlineLight,
    outlineVariant = OutlineVariantLight
)

@Composable
fun EPoddTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colorScheme = if (darkTheme) EPodDarkColorScheme else EPodLightColorScheme

    MaterialTheme(
        colorScheme = colorScheme,
        shapes = Shapes,
        typography = Typography,
        content = content
    )
}