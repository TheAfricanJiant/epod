---
name: e-Pod Core
colors:
  surface: '#f8f9fa'
  surface-dim: '#d9dadb'
  surface-bright: '#f8f9fa'
  surface-container-lowest: '#ffffff'
  surface-container-low: '#f3f4f5'
  surface-container: '#edeeef'
  surface-container-high: '#e7e8e9'
  surface-container-highest: '#e1e3e4'
  on-surface: '#191c1d'
  on-surface-variant: '#3d4a3e'
  inverse-surface: '#2e3132'
  inverse-on-surface: '#f0f1f2'
  outline: '#6c7b6d'
  outline-variant: '#bbcbbb'
  surface-tint: '#006d37'
  primary: '#006d37'
  on-primary: '#ffffff'
  primary-container: '#2ecc71'
  on-primary-container: '#005027'
  inverse-primary: '#4ae183'
  secondary: '#4b6076'
  on-secondary: '#ffffff'
  secondary-container: '#cce2fc'
  on-secondary-container: '#50657b'
  tertiary: '#8c4f10'
  on-tertiary: '#ffffff'
  tertiary-container: '#f0a25d'
  on-tertiary-container: '#6b3800'
  error: '#ba1a1a'
  on-error: '#ffffff'
  error-container: '#ffdad6'
  on-error-container: '#93000a'
  primary-fixed: '#6bfe9c'
  primary-fixed-dim: '#4ae183'
  on-primary-fixed: '#00210c'
  on-primary-fixed-variant: '#005228'
  secondary-fixed: '#cfe5ff'
  secondary-fixed-dim: '#b3c9e2'
  on-secondary-fixed: '#051d30'
  on-secondary-fixed-variant: '#34495e'
  tertiary-fixed: '#ffdcc2'
  tertiary-fixed-dim: '#ffb77b'
  on-tertiary-fixed: '#2e1500'
  on-tertiary-fixed-variant: '#6d3a00'
  background: '#f8f9fa'
  on-background: '#191c1d'
  surface-variant: '#e1e3e4'
  circuit-green: '#2ECC71'
  recycled-slate: '#34495E'
  copper-hardware: '#B87333'
  lithium-grey: '#4A4A4A'
  surface-translucent: rgba(248, 249, 250, 0.7)
typography:
  headline-lg:
    fontFamily: Hanken Grotesk
    fontSize: 32px
    fontWeight: '700'
    lineHeight: 40px
    letterSpacing: -0.02em
  headline-md:
    fontFamily: Hanken Grotesk
    fontSize: 24px
    fontWeight: '600'
    lineHeight: 32px
  body-lg:
    fontFamily: Hanken Grotesk
    fontSize: 18px
    fontWeight: '400'
    lineHeight: 28px
  body-md:
    fontFamily: Hanken Grotesk
    fontSize: 16px
    fontWeight: '400'
    lineHeight: 24px
  label-mono:
    fontFamily: JetBrains Mono
    fontSize: 14px
    fontWeight: '500'
    lineHeight: 20px
    letterSpacing: 0.05em
  label-sm-mono:
    fontFamily: JetBrains Mono
    fontSize: 12px
    fontWeight: '500'
    lineHeight: 16px
  headline-lg-mobile:
    fontFamily: Hanken Grotesk
    fontSize: 28px
    fontWeight: '700'
    lineHeight: 36px
rounded:
  sm: 0.25rem
  DEFAULT: 0.5rem
  md: 0.75rem
  lg: 1rem
  xl: 1.5rem
  full: 9999px
spacing:
  unit: 8px
  margin-mobile: 16px
  margin-desktop: 24px
  gutter: 16px
  component-gap-xs: 4px
  component-gap-sm: 8px
  component-gap-md: 16px
  section-gap: 32px
---

## Brand & Style

The design system embodies **Industrial Eco-Minimalism**—a narrative of "Exposed Circularity" where the digital interface honors the physical salvaged hardware. The brand personality is technical, resourceful, and transparent, bridging the gap between high-end audio engineering and DIY hacker culture.

The visual style is a hybrid of **Minimalism** and **Tactile Glassmorphism**. It utilizes clean, expansive light-gray surfaces to maintain a premium feel, while semi-transparent layers and subtle grain textures evoke 3D-printed rPETG and internal copper shielding. The interface doesn't hide complexity; it celebrates it through organized telemetry and raw data visualization, ensuring the user feels connected to the "guts" of their sustainable device.

## Colors

The palette is driven by the physical components of the e-Pod. 

- **Primary (Circuit Green):** Used for active states, successful connections, and primary action buttons. It represents the "pulse" of the device.
- **Secondary (Recycled Slate):** Used for structural elements, headers, and grounded UI components. It provides the industrial weight needed to balance the bright accents.
- **Tertiary (Copper Hardware):** Reserved for technical details, telemetry highlights, and decorative accents that mimic internal RF shielding.
- **Neutral:** A range of ultra-light grays and "Recycled Clear" translucents that form the background surfaces.

Color is used semantically to reflect hardware states: Green for stable BLE connection, Amber (derived from Copper) for "Connecting," and Slate for "Disconnected."

## Typography

The typography strategy pairs the functional precision of **Hanken Grotesk** for primary UI elements with the technical aesthetic of **JetBrains Mono** for hardware-related data.

- **Headlines & Body:** Use Hanken Grotesk. It offers a modern, sharp geometric feel that aligns with industrial design while remaining highly legible for track listings and settings.
- **Hardware Telemetry:** All MAC addresses, RSSI values, file sizes, and battery percentages must use JetBrains Mono. This creates a clear visual distinction between "User Content" (music) and "System Data" (hardware).
- **Scale:** High contrast in weights (Bold for titles, Regular for body) ensures accessibility on the move.

## Layout & Spacing

The layout follows a **Fluid Grid** model based on an 8px rhythmic unit, optimized for high-density information. 

- **Mobile First:** Content is primarily oriented along a central vertical axis, mirroring the physical e-Pod form factor. Margins are fixed at 16px to maximize screen real estate for technical logs.
- **Visual Weight:** Spacing is tighter (4px-8px) within functional groups (e.g., a play button and its progress bar) and more generous (24px-32px) between distinct hardware modules (e.g., "Now Playing" vs. "Battery Diagnostics").
- **Adaptation:** On larger screens, the layout shifts to a multi-column display where telemetry data and media controls sit side-by-side rather than stacking.

## Elevation & Depth

Elevation is conveyed through **Tonal Layering** and **Material Translucency** rather than traditional drop shadows.

- **Background (Level 0):** Neutral light gray with a subtle "rPETG grain" texture overlay.
- **Cards & Containers (Level 1):** Solid white or semi-transparent (70% opacity) surfaces with a low-contrast 1px border (#E0E0E0).
- **Active Elements (Level 2):** Use Tonal Elevation—a slight shift in background color (e.g., a light green tint) to indicate "Connected" or "Active" states.
- **Glassmorphism:** Use Backdrop Blurs (10px - 20px) for navigation bars and overlays to simulate the "Recycled Clear" plastic housing of the physical player. This keeps the internal "guts" of the app (the background content) partially visible.

## Shapes

The shape language is a direct nod to the "Retro iPod" silhouette—rational and geometric with softened corners.

- **Main Containers:** Use a 1rem (16px) radius to echo the physical shell of the player.
- **Buttons & Chips:** Use a full pill-shape for actions like "Connect" or "Scan" to provide a friendly, tactile target.
- **Hardware Cards:** Use 0.5rem (8px) for internal cards (e.g., individual track items in a list) to maintain a tighter, organized industrial appearance.
- **Interactive States:** On press, elements should slightly shrink (scale: 0.98) to mimic the mechanical "click" of the salvaged tactile buttons used in the hardware.

## Components

- **Buttons:** Primary buttons use a solid "Circuit Green" fill with white Hanken Grotesk Medium text. Secondary buttons use a "Recycled Slate" outline.
- **The "Track Card":** A centered, elevated card with high-contrast typography. It includes a 1px "Copper" accent line that animates during playback.
- **Telemetry Chips:** Small, pill-shaped labels using JetBrains Mono. Use color-coded backgrounds for status (e.g., Green for "Good Signal," Red for "Error 133").
- **Progress Bars:** Dual-layered bars. The background represents total capacity (Slate) and the fill represents current state (Circuit Green). Use a subtle "stipple" pattern on the fill to mimic 3D printed textures.
- **Input Fields:** Minimalist with only a bottom border (2px Slate) that turns Primary Green on focus. Labels should be small and monospaced.
- **The Circularity Audit Icon:** A unique, QR-inspired decorative element present in the footer of the app, linking to the hardware's sustainability metrics.