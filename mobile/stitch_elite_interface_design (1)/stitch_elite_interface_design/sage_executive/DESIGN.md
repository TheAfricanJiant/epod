---
name: Sage Executive
colors:
  surface: '#faf9f6'
  surface-dim: '#dbdad7'
  surface-bright: '#faf9f6'
  surface-container-lowest: '#ffffff'
  surface-container-low: '#f4f3f1'
  surface-container: '#efeeeb'
  surface-container-high: '#e9e8e5'
  surface-container-highest: '#e3e2e0'
  on-surface: '#1a1c1a'
  on-surface-variant: '#434843'
  inverse-surface: '#2f312f'
  inverse-on-surface: '#f2f1ee'
  outline: '#737872'
  outline-variant: '#c3c8c1'
  surface-tint: '#506354'
  primary: '#334537'
  on-primary: '#ffffff'
  primary-container: '#4a5d4e'
  on-primary-container: '#c0d5c2'
  inverse-primary: '#b7ccb9'
  secondary: '#526350'
  on-secondary: '#ffffff'
  secondary-container: '#d5e8cf'
  on-secondary-container: '#586955'
  tertiary: '#454138'
  on-tertiary: '#ffffff'
  tertiary-container: '#5d584e'
  on-tertiary-container: '#d6cec2'
  error: '#ba1a1a'
  on-error: '#ffffff'
  error-container: '#ffdad6'
  on-error-container: '#93000a'
  primary-fixed: '#d3e8d5'
  primary-fixed-dim: '#b7ccb9'
  on-primary-fixed: '#0e1f13'
  on-primary-fixed-variant: '#394b3d'
  secondary-fixed: '#d5e8cf'
  secondary-fixed-dim: '#b9ccb4'
  on-secondary-fixed: '#101f10'
  on-secondary-fixed-variant: '#3b4b39'
  tertiary-fixed: '#eae1d5'
  tertiary-fixed-dim: '#cdc5ba'
  on-tertiary-fixed: '#1f1b14'
  on-tertiary-fixed-variant: '#4b463d'
  background: '#faf9f6'
  on-background: '#1a1c1a'
  surface-variant: '#e3e2e0'
typography:
  headline-xl:
    fontFamily: Hanken Grotesk
    fontSize: 40px
    fontWeight: '600'
    lineHeight: 48px
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
    fontWeight: '500'
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
  unit: 8px
  container-padding: 24px
  gutter: 16px
  section-gap: 48px
  stack-sm: 4px
  stack-md: 12px
  stack-lg: 24px
---

## Brand & Style

This design system embodies a "soft mature" aesthetic, pivoting away from high-energy tech visuals toward a sophisticated, eco-conscious professional atmosphere. The brand personality is grounded, reliable, and premium, targeting executive users who value sustainability and understated quality.

The design style is **Modern Minimalism** infused with **Tactile/Matte** qualities. It avoids harsh digital perfections in favor of organic warmth. Surfaces should feel physical and "soft-touch," achieved through low-contrast transitions and a curated palette of earth-toned neutrals. The emotional response is one of calm authority and environmental stewardship.

## Colors

The palette transitions from "Circuit Green" to a sophisticated **Forest Green** (Primary) and **Muted Sage** (Secondary). These colors are paired with a "Matte" foundation of **Off-White** and **Warm Grey** to eliminate the sterile feel of standard interface greys.

- **Primary (#4A5D4E):** Used for key actions and brand presence. It is deep enough to provide accessible contrast against light backgrounds.
- **Secondary (#8FA18B):** A softer sage used for secondary highlights, active states, and decorative elements.
- **Surface Neutrals:** Use `#FAF9F6` (Bone) for the main background and `#EBE9E4` (Pebble) for containers and input backgrounds. 
- **Text:** Avoid pure black. Use `#2D312E` to maintain a soft, legible contrast that feels integrated with the green palette.

## Typography

This design system leverages **Hanken Grotesk** for its clean, contemporary character, but adjusts its application to feel more "organic." 

- **Weighting:** Headlines utilize Medium (500) and Semi-Bold (600) weights rather than heavy Bold to maintain a refined executive look.
- **Scaling:** Large display text uses tighter letter-spacing to appear more considered and "designed."
- **Body Text:** Increased line-heights ensure a relaxed, breathable reading experience, supporting the "soft mature" narrative.
- **Readability:** All text is rendered with a slightly softened contrast (using the dark green-grey instead of black) to reduce eye strain.

## Layout & Spacing

The layout philosophy follows a **Fluid Grid** model with generous safe areas to reinforce the premium, "un-cluttered" feel.

- **Rhythm:** An 8px base unit drives all spacing. 
- **Margins:** Use wide horizontal margins (minimum 24px on mobile, scaling up to 80px on desktop) to allow content to "breathe" in the center of the screen.
- **Hierarchy:** Group related elements with 12px or 24px stacks, but maintain 48px+ gaps between major sections to prevent the UI from feeling dense or "industrial."
- **Mobile Adaptivity:** Gutters reduce to 12px on mobile to maximize horizontal real estate, while vertical section gaps remain relatively high to keep the "matte" airy feel.

## Elevation & Depth

To achieve the "soft mature" look, this design system moves away from traditional drop shadows in favor of **Tonal Layers** and **Soft Ambient Occlusion**.

- **Surface Strategy:** Depth is primarily communicated through subtle color shifts between the background (`#FAF9F6`) and surface containers (`#EBE9E4`). 
- **Shadows:** When necessary for functional elevation (like modals or floating menus), use highly diffused shadows with a tint of the primary green: `rgba(74, 93, 78, 0.08)`. Shadows should have a large blur radius (20px+) and no harsh edges.
- **Interactive Depth:** Buttons should not "pop" out aggressively; instead, use slight shifts in background saturation or 1px internal borders to indicate pressability.

## Shapes

The shape language is consistently **Rounded**, avoiding both the aggression of sharp corners and the "juvenile" feel of full pill shapes.

- **Standard Elements:** Buttons, cards, and input fields use a 0.5rem (8px) radius.
- **Large Containers:** Section headers or large cards may scale up to a 1rem (16px) radius to emphasize the organic, soft feel.
- **Consistency:** Maintain uniform corner radii across all form elements to ensure the UI feels like a singular, coherent product.

## Components

- **Buttons:** Primary buttons use the Forest Green background with Off-White text. Secondary buttons use a Sage outline or a subtle Warm Grey fill. Avoid "glow" or heavy gradients; stick to flat, matte fills.
- **Inputs:** Use the Warm Grey (`#EBE9E4`) for input backgrounds rather than white. This reduces the "bright" screen effect and enhances the matte aesthetic. On focus, use a 2px Sage border.
- **Cards:** Cards should be defined by their background color (`#EBE9E4`) rather than heavy borders or shadows. Keep them flat against the main background for a seamless, architectural look.
- **Chips/Labels:** Use a very light tint of Sage with Forest Green text. Keep these small and use the "Medium" font weight for clarity.
- **Data Visualization:** Use the primary green and its tints. Avoid typical "dashboard colors" (bright red/yellow) unless absolutely necessary for warnings, preferring muted amber or terracota for alerts to keep the palette sophisticated.