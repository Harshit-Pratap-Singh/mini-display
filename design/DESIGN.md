---
name: Embedded Telemetry TFT
colors:
  surface: '#111318'
  surface-dim: '#111318'
  surface-bright: '#37393e'
  surface-container-lowest: '#0c0e13'
  surface-container-low: '#191c20'
  surface-container: '#1d2024'
  surface-container-high: '#272a2f'
  surface-container-highest: '#32353a'
  on-surface: '#e1e2e9'
  on-surface-variant: '#bdc8d1'
  inverse-surface: '#e1e2e9'
  inverse-on-surface: '#2e3035'
  outline: '#87929a'
  outline-variant: '#3e484f'
  surface-tint: '#7bd0ff'
  primary: '#8ed5ff'
  on-primary: '#00354a'
  primary-container: '#38bdf8'
  on-primary-container: '#004965'
  inverse-primary: '#00668a'
  secondary: '#ffb95f'
  on-secondary: '#472a00'
  secondary-container: '#ee9800'
  on-secondary-container: '#5b3800'
  tertiary: '#56e5a9'
  on-tertiary: '#003824'
  tertiary-container: '#30c88f'
  on-tertiary-container: '#004e34'
  error: '#ffb4ab'
  on-error: '#690005'
  error-container: '#93000a'
  on-error-container: '#ffdad6'
  primary-fixed: '#c4e7ff'
  primary-fixed-dim: '#7bd0ff'
  on-primary-fixed: '#001e2c'
  on-primary-fixed-variant: '#004c69'
  secondary-fixed: '#ffddb8'
  secondary-fixed-dim: '#ffb95f'
  on-secondary-fixed: '#2a1700'
  on-secondary-fixed-variant: '#653e00'
  tertiary-fixed: '#6ffbbe'
  tertiary-fixed-dim: '#4edea3'
  on-tertiary-fixed: '#002113'
  on-tertiary-fixed-variant: '#005236'
  background: '#111318'
  on-background: '#e1e2e9'
  surface-variant: '#32353a'
typography:
  headline-lg:
    fontFamily: JetBrains Mono
    fontSize: 24px
    fontWeight: '700'
    lineHeight: 28px
    letterSpacing: -0.02em
  headline-md:
    fontFamily: JetBrains Mono
    fontSize: 18px
    fontWeight: '700'
    lineHeight: 22px
    letterSpacing: -0.01em
  headline-sm:
    fontFamily: JetBrains Mono
    fontSize: 14px
    fontWeight: '600'
    lineHeight: 18px
    letterSpacing: 0em
  body-lg:
    fontFamily: Inter
    fontSize: 13px
    fontWeight: '500'
    lineHeight: 16px
    letterSpacing: 0em
  body-md:
    fontFamily: Inter
    fontSize: 11px
    fontWeight: '400'
    lineHeight: 14px
    letterSpacing: 0.01em
  body-sm:
    fontFamily: Inter
    fontSize: 9px
    fontWeight: '500'
    lineHeight: 12px
    letterSpacing: 0.02em
  label-lg:
    fontFamily: JetBrains Mono
    fontSize: 11px
    fontWeight: '600'
    lineHeight: 14px
    letterSpacing: 0.04em
  label-md:
    fontFamily: JetBrains Mono
    fontSize: 9px
    fontWeight: '600'
    lineHeight: 12px
    letterSpacing: 0.06em
  label-sm:
    fontFamily: JetBrains Mono
    fontSize: 8px
    fontWeight: '700'
    lineHeight: 10px
    letterSpacing: 0.08em
rounded:
  sm: 0.125rem
  DEFAULT: 0.25rem
  md: 0.375rem
  lg: 0.5rem
  xl: 0.75rem
  full: 9999px
spacing:
  gutter: 0.375rem
  margin: 0.5rem
  space-xs: 0.125rem
  space-sm: 0.25rem
  space-md: 0.375rem
  space-lg: 0.5rem
  space-xl: 0.75rem
---

## Brand & Style

This design system is engineered for micro-scale, high-density embedded displays (such as 240x240 square and circular TFT/IPS screens powered by GC9A01 or ST7789 drivers). The visual philosophy merges industrial instrumentation with crisp, mission-critical digital readout aesthetics.

### Aesthetic Persona
- **Direct & Instrument-Grade:** Pure information architecture free from decorative fluff. Every pixel serves a data point, status alert, or telemetry readback.
- **High-Density Legibility:** Optimized for micro-viewing angles, sunlight readability, and rapid peripheral scanning.
- **Precision Dark-Field:** True deep blacks eliminate optical bleed and mimic physical glass bezels, allowing luminous neon phosphor accents to punch forward with tactile precision.

## Colors

The color palette centers around ultra-deep, high-contrast black backdrops paired with luminescent digital telemetry tones:

- **Primary (`#38BDF8` - Electric Cyan):** Active channel selection, operational telemetry, vector paths, and dominant numeric reads.
- **Secondary (`#F59E0B` - Solar Amber):** System warnings, thermal indicators, dynamic state changes, and elevated load monitors.
- **Tertiary (`#10B981` - Mint Emerald):** Nominal statuses, synced states, battery health indicators, and valid protocol confirmations.
- **Surface & Canvas (`#05070B` - OLED Void):** The foundational pure-black void that guarantees infinite contrast ratio against vibrant LEDs.
- **Support Neutral (`#0F172A` - Hard Slate):** Used for panel containers, inactive slot wells, and baseline track meters.
- **High Readout Neutral (`#F8FAFC` - Stark White):** Primary alphanumeric data labels, unit indicators, and mission-critical scalar figures.

## Typography

Typography prioritizes sub-millimeter legibility on miniature panels:

- **JetBrains Mono** powers numeric telemetry, status flags, and readout badges. Its strict tabular spacing prevents layout jitter during real-time scalar fluctuations.
- **Inter** handles secondary context, category descriptors, and ancillary metadata, providing anti-aliased legibility at tiny 9px to 11px scales.
- All micro labels (`label-sm`, `label-md`) strictly employ uppercase casing with expanded tracking to avoid glyph collision on low-DPI SPI-driven hardware displays.

## Layout & Spacing

Designed around a micro-footprint constraint (e.g., 240x240 physical matrix):

- **Grid Framework:** A 4-column micro-grid with a standardized 6px (`0.375rem`) gutter and 8px (`0.5rem`) viewport boundary margin. Circular displays enforce a supplemental 16px corner keep-out inset to avoid hardware bezel clipping.
- **Density Tiering:** Spacing intervals scale incrementally by 2px to 4px units (`0.125rem` to `0.75rem`). Elements cluster into modular micro-tiles to maximize the visible data density without optical collision.
- **Layout Adaptability:** The canvas dynamically pivots between square quadrant quadrants (120x120 sub-cells) and stacked longitudinal telemetry strips (240x60 ribbons).

## Elevation & Depth

To preserve ultra-crisp hardware performance and render pipeline efficiency on embedded microcontrollers, this design system completely eliminates diffuse dropshadows and heavy blur effects in favor of:

1. **Precision Structural Outlines:** Hierarchy is established through 1px crisp borders using `#1E293B` for resting states and `#38BDF8` for active/focused telemetry blocks.
2. **Layered Value Tiers:** Depths are stepped cleanly from `#05070B` (canvas base) to `#0B0F17` (sub-panel containers) to `#131C2E` (interactive active cells).
3. **Photon Accents:** Selective single-pixel top highlight borders or phosphor corner tick-marks in vibrant Cyan or Solar Amber convey active focus and hardware level state.

## Shapes

The shape system employs tight, technical corner radii (`roundedness: 1`):
- Base containers and micro-cards utilize a compact 4px (`0.25rem`) border radius, ensuring that maximum screen area remains dedicated to legible pixel matrix data.
- Small status pips and indicator tags use 2px inner radii or exact square cuts to evoke physical chip packages and PCB silkscreen geometries.

## Components

### Micro-Cards & Tile Containers
- Encased with a solid 1px border (`#1E293B`) on `#0B0F17` background.
- Include an optional 3px vertical status stripe on the left edge denoting functional domains (Cyan for sensor telemetry, Amber for battery/power, Emerald for network).

### Telemetry Badges & Weather Indicators
- Composed of uppercase `label-sm` monospaced text paired with minimalist line-based micro vector glyphs (10x10px).
- Value readouts sit within `#0F172A` recessed wells with 2px padding.

### Digital Progress & Level Gauges
- Horizontal segmented bars with 1px dark separators creating physical LCD-segmentation visual cues.
- Fill uses `#38BDF8` for operational levels, transitioning smoothly to `#F59E0B` above 85% capacity.

### Buttons & Rotary Target Selectors
- Outer frame uses 1px solid `#38BDF8` with a subtle `#38BDF8` 10% alpha fill when highlighted.
- Text switches to high-contrast white `#F8FAFC` with bold monospaced alignment.