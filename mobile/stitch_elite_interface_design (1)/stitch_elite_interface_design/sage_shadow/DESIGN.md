---
name: Sage Shadow
colors:
  surface: '#121416'
  surface-dim: '#121416'
  surface-bright: '#37393b'
  surface-container-lowest: '#0c0e10'
  surface-container-low: '#1a1c1e'
  surface-container: '#1e2022'
  surface-container-high: '#282a2c'
  surface-container-highest: '#333537'
  on-surface: '#e2e2e5'
  on-surface-variant: '#c3c8bd'
  inverse-surface: '#e2e2e5'
  inverse-on-surface: '#2f3133'
  outline: '#8d9289'
  outline-variant: '#434840'
  surface-tint: '#b1cfa7'
  primary: '#c3e2ba'
  on-primary: '#1d361a'
  primary-container: '#a8c69f'
  on-primary-container: '#395334'
  inverse-primary: '#4a6545'
  secondary: '#bec8cc'
  on-secondary: '#283235'
  secondary-container: '#3e484c'
  on-secondary-container: '#acb7ba'
  tertiary: '#ded9cb'
  on-tertiary: '#323027'
  tertiary-container: '#c2bdb0'
  on-tertiary-container: '#4f4c42'
  error: '#ffb4ab'
  on-error: '#690005'
  error-container: '#93000a'
  on-error-container: '#ffdad6'
  primary-fixed: '#ccebc2'
  primary-fixed-dim: '#b1cfa7'
  on-primary-fixed: '#082007'
  on-primary-fixed-variant: '#334d2f'
  secondary-fixed: '#dae4e8'
  secondary-fixed-dim: '#bec8cc'
  on-secondary-fixed: '#131d20'
  on-secondary-fixed-variant: '#3e484c'
  tertiary-fixed: '#e7e2d4'
  tertiary-fixed-dim: '#cbc6b9'
  on-tertiary-fixed: '#1d1c13'
  on-tertiary-fixed-variant: '#49473c'
  background: '#121416'
  on-background: '#e2e2e5'
  surface-variant: '#333537'
typography:
  display-lg:
    fontFamily: Hanken Grotesk
    fontSize: 56px
    fontWeight: '700'
    lineHeight: 64px
    letterSpacing: -0.02em
  headline-lg:
    fontFamily: Hanken Grotesk
    fontSize: 32px
    fontWeight: '600'
    lineHeight: 40px
    letterSpacing: -0.01em
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
  label-md:
    fontFamily: Hanken Grotesk
    fontSize: 14px
    fontWeight: '500'
    lineHeight: 20px
    letterSpacing: 0.02em
  headline-lg-mobile:
    fontFamily: Hanken Grotesk
    fontSize: 28px
    fontWeight: '600'
    lineHeight: 36px
rounded:
  sm: 0.25rem
  DEFAULT: 0.5rem
  md: 0.75rem
  lg: 1rem
  xl: 1.5rem
  full: 9999px
spacing:
  base: 8px
  container-max: 1280px
  gutter: 24px
  margin-mobile: 16px
  margin-desktop: 48px
---

## Brand & Style

This design system is a "Dark Soft" evolution of executive aesthetics, pivoting from traditional light corporate styles to a sophisticated, eye-friendly matte environment. It targets a professional audience that values sustainability, high-end precision, and long-form focus.

The visual style is **Minimalist with Tactile Mattes**. It avoids the harshness of pure black (#000) in favor of deep charcoal and midnight tones that absorb light rather than reflect it. Surfaces are defined by soft tonal shifts and subtle depth rather than sharp borders. The emotional response should be one of calm authority—like a luxury EV interior at night.

## Colors

The palette is anchored by a deep charcoal neutral that serves as the "matte" canvas. 

- **Primary (Sage):** A desaturated, luminous sage green optimized for dark backgrounds. It is used for calls to action, active states, and brand moments.
- **Secondary (Slate):** A mid-tone charcoal used for surface layering and secondary containers.
- **Tertiary (Warm Grey):** Used sparingly for subtle accents or low-priority text to maintain the "warmth" of the dark mode.
- **Neutral:** A midnight matte base (#1A1C1E) that ensures the UI feels soft rather than clinical.

Avoid high-vibrancy saturations; all colors should feel slightly "dusted" to maintain the executive persona.

## Typography

This design system utilizes **Hanken Grotesk** across all levels to project a modern, engineered clarity. 

- **Color Contrast:** Headers use an off-white (#F5F5F5) for maximum impact, while body text uses a warm-tinted light grey (#C5C5C5) to reduce eye strain.
- **Weighting:** Headlines are set with tight letter spacing and heavier weights to anchor the page. 
- **Readability:** Body text employs a generous line-height to ensure the dark background doesn't "swallow" the glyphs.

## Layout & Spacing

The design system follows a **Fixed-Fluid Hybrid** model. Content is contained within a 1280px max-width grid on desktop, centered with wide margins to emphasize a premium, focused feel.

- **Grid:** A 12-column grid is used for desktop, transitioning to a 4-column grid for mobile.
- **Rhythm:** Spacing follows an 8px linear scale. Large sections should be separated by 80px or 96px to maintain the minimalist breathability.
- **Negative Space:** Elements are grouped with tighter internal padding (16-24px) but separated by large external margins to create "islands" of information.

## Elevation & Depth

Depth is conveyed through **Tonal Layering** rather than traditional drop shadows.

1.  **Level 0 (Base):** The deepest charcoal (#1A1C1E).
2.  **Level 1 (Surface):** Slightly lighter (#232629) with a very soft, large-radius ambient shadow (Opacity 20%).
3.  **Level 2 (Interaction):** A subtle linear gradient (Top: #2C3034 to Bottom: #232629) to create a matte-convex effect.

Shadows, when used, are never pure black; they are tinted with the primary sage or a deep navy to keep the "soft" feel. Use backdrop blurs (12px-20px) for floating overlays or navigation bars to maintain context of the underlying layers.

## Shapes

The shape language is **Refined and Intentional**. 

Standard elements like input fields and buttons use a 0.5rem (8px) radius. Larger cards or sections use "rounded-lg" (16px) or "rounded-xl" (24px) to soften the overall interface. This moderate roundedness bridges the gap between technical "sharp" design and friendly "pill" design, fitting for an executive context.

## Components

- **Buttons:** Primary buttons use the luminous Sage Green with dark-charcoal text. Secondary buttons are "Ghost" style with a 1px border in the secondary slate color.
- **Input Fields:** Use a subtle inset shadow to appear "carved" into the surface. The active state is a thin 1px Sage Green border.
- **Cards:** Cards should not have high-contrast borders. Use a subtle fill color change (Level 1 Surface) and a very soft 48px blur shadow to lift them from the background.
- **Chips:** Small, low-profile elements with 20% opacity Sage Green backgrounds and full-opacity Sage text.
- **Lists:** Separated by thin, low-opacity (10%) dividers in warm grey.
- **Data Visualization:** Use the primary sage green as the hero data color, supported by desaturated teals and slates for secondary data points.