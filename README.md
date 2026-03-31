# ECHO-TR v1.4

<br/><br/>

<img width="451" height="673" alt="image" src="https://github.com/user-attachments/assets/4d47cea5-03c9-441e-83a8-a92cfdfcd842" />



<br/><br/>

ECHO-TR is a creative delay effect built for texture generation, tonal manipulation, and pitch-shifted harmonics.  
It combines forward and reverse delay with MIDI-controlled pitch, envelope-feedback dynamics, and a minimal CRT-inspired interface.

## Concept

ECHO-TR treats delay not as a mixing utility but as an instrument. By feeding MIDI notes into the delay engine, the delay time maps directly to pitch — turning the feedback loop into a resonator that can play melodies, drones, and evolving textures.

The reverse mode reads audio backward in chunks while keeping the feedback path forward and coherent. This means reverse tails behave identically to direct mode — only the output is reversed.

Envelope feedback adds an envelope that resets on every note change, letting the feedback "swell" back in naturally. The result is a self-clearing delay that never muddies across pitch changes.

## Interface

ECHO-TR uses a text-based UI with horizontal bar sliders. All controls are visible at once — no pages, tabs, or hidden menus.

- **Bar sliders**: Click and drag horizontally. Right-click for numeric entry (except STYLE, which is slider-only).
- **Toggle buttons**: SYNC, MIDI, ENV FBK, RVS (reverse). Click to enable/disable.
- **Sub-labels**: Click the text next to MIDI, ENV FBK, or RVS to open their configuration prompt.
- **Collapsible INPUT/OUTPUT/MIX section**: Click the toggle bar (triangle) at the top of the slider area to swap between main parameters and the INPUT, OUTPUT, MIX controls. The toggle bar stays fixed in place; only the arrow direction changes. State persists across sessions and preset changes.
- **Filter bar**: Visible in the INPUT/OUTPUT/MIX section. Click to open the HP/LP filter configuration prompt with frequency, slope, and enable/disable controls for each filter.
- **Gear icon** (top-right): Opens the info popup with version, credits, and a link to Graphics settings.
- **Graphics popup**: Toggle CRT post-processing effect and switch between default/custom colour palettes.
- **Resize**: Drag the bottom-right corner. Size persists across sessions.

The value column to the right of each slider shows the current state in context:
- TIME shows milliseconds (or MIDI note name when active, or sync division).
- FEEDBACK shows percentage + "FBK".
- STYLE shows MONO/STEREO/WIDE/DUAL/PING-PONG.
- MOD shows the frequency multiplier.
- INPUT/OUTPUT show dB values.
- MIX shows percentage.

## Parameters

### TIME (0–5000 ms)

Manual delay time. Overridden by MIDI or SYNC when active.  
Smoothed per-sample via exponential moving average (80 ms time constant) for glitch-free pitch sweeps.

When MIDI is active, TIME shows the note name instead of milliseconds. When the note releases, the delay glides back to the manual TIME knob value.

### FEEDBACK (−100 to +100%)

Signal fed back into the delay line. 100% = infinite sustain / self-oscillation. Negative values invert the feedback polarity, producing pitch-inverted repetitions and a different comb-filter character.  
Sign-preserving smoothstep mapping (3x²−2x³): both extremes have finer resolution — especially near ±100% where self-oscillation lives.  
Only a DC blocker (5 Hz high-pass) sits in the feedback path — no filtering, no saturation. Maximally transparent.

### STYLE

Routing topology for the delay:
- **MONO**: Single delay line, summed to both channels.
- **STEREO**: Independent left/right delay lines.
- **WIDE**: Cross-feedback between channels with pitch-compensated octave offset. The delays are scaled so that the cross-feedback round trip (T_L + T_R) equals the user’s delay time T, keeping the comb-filter resonance at the same pitch as STEREO mode. The ratio between channels is 2:1 (L = 2T/3, R = T/3), preserving the octave character that spreads repetitions across the stereo field. Unlike DUAL (which uses a ×0.5 ratio with independent feedback), WIDE uses cross-feedback — each channel feeds into the other — creating a widening bounce pattern at the correct pitch.
- **DUAL**: Independent left/right delay lines with the right channel at half the delay time (×0.5). No cross-feedback — each channel repeats independently at different rates.
- **PING-PONG**: Cross-feedback with mono-summed input fed into the left channel only. Each repetition alternates fully between left and right.

All modes share the same interpolation (4-point Hermite) and feedback processing.

### MOD (0–100%)

Frequency multiplier applied to the delay time.  
0% = ×0.25 (4× longer delay), 50% = ×1.0 (no change), 100% = ×4.0 (4× shorter delay).  
Useful for octave shifting, harmonic tuning, and detuned textures.

### INPUT (−100 to 0 dB)

Pre-delay gain. Controls how much signal enters the delay line.

### OUTPUT (−100 to +24 dB)

Post-delay gain. Applied to the wet signal only.

### MIX (0–100%)

Dry/wet balance. 0% = fully dry, 100% = fully wet.

### HP/LP FILTER

High-pass and low-pass filters applied to the wet signal, accessible via the filter bar in the IO section.

- **HP FREQ (20–20 000 Hz)**: High-pass cutoff frequency.
- **LP FREQ (20–20 000 Hz)**: Low-pass cutoff frequency.
- **HP SLOPE (6 dB / 12 dB / 24 dB)**: High-pass filter slope.
- **LP SLOPE (6 dB / 12 dB / 24 dB)**: Low-pass filter slope.
- **HP / LP toggles**: Enable or disable each filter independently. Click the HP/LP label or its checkbox to toggle.

Slope modes:
- **6 dB/oct**: Single-pole filter.
- **12 dB/oct**: Second-order Butterworth.
- **24 dB/oct**: Two cascaded second-order Butterworth stages.

### SYNC

Locks delay time to DAW tempo. Provides 30 musical subdivisions:  
1/64 through 8/1, each with triplet, normal, and dotted variants.  
Disabled when MIDI is active (MIDI takes priority).

### MIDI

Enables MIDI note control of delay time. Incoming notes set delay time to `1000 / frequency` ms.  
Example: A4 (440 Hz) → 2.27 ms.

**Velocity → Glide**: Note velocity controls the portamento speed between pitch changes.
- vel 127 → instant transition (~0.2 ms).
- vel 1 → full glide (~200 ms).
- The curve is designed so most of the playable range (vel 40–127) feels instant, with glide only appearing at extreme pianissimo.

Direct and reverse modes use independent velocity curves calibrated so the perceived glide is identical despite their different architectures.

**MIDI Channel**: Click the channel display to select channel 1–16, or OMNI (all channels).

### ENV FBK

Envelope feedback. When enabled, feedback resets to zero on every note/time/MOD change, then ramps back to the user's feedback setting.

This prevents muddy buildup between pitch changes while preserving self-oscillation during sustained notes.

**TAU (0–100%)**: Recovery speed.  
0% = fast recovery (30 ms), 100% = slow dramatic swell (3 s).  
Automatic pitch-scaling: shorter delays recover faster (`√(delay/1000)`).

**ATT (0–100%)**: Modulation depth.  
0% = envelope bypassed (feedback always at full), 100% = maximum suppression on reset.  
Cubic curve ensures gradual onset. UI 100% maps to internal 75% to keep the full range usable.

### RVS (Reverse)

Reverse delay mode. Reads audio backward in chunks, producing reversed playback.

The feedback path reads **forward** and mirrors the current STYLE routing (cross-feedback for WIDE/PING-PONG, independent for others, mono-sum for MONO, etc.), so the delay buffer always contains coherent audio. Only the output is reversed. This means:
- Tails behave the same as direct mode for the selected STYLE.
- Self-oscillation works naturally.
- Switching between modes doesn't corrupt the buffer.
- WIDE/DUAL use per-channel chunk lengths matching their forward-mode delay times.

Each chunk's length equals the current (smoothed) delay time. At chunk boundaries, the next chunk adopts the latest delay value, providing natural pitch tracking.

**SMOOTH (−2 to +2)**: Controls the output taper at chunk edges.  
Maps to a multiplier: 2^(value).  
- −2 = ×0.25 (very short taper → choppy, granular).
- 0 = ×1.0 (clean default).
- +2 = ×4.0 (long taper → ambient, washy).

The taper is **proportional** to chunk length (1/16th × multiplier) so high MIDI notes (short chunks) are never silenced by a fixed-length taper.

### ENGINE

Delay character mode:
- **CLEAN** (default): Transparent digital delay. No coloration.
- **TAPE**: Applies subtle saturation and frequency-dependent rolloff to the feedback path, emulating analog tape delay.
- **BBD**: Emulates bucket-brigade device delays with band-limited frequency response and mild distortion characteristics.

### TILT (−6 to +6 dB)

Spectral tilt applied to the wet signal. A first-order symmetric shelf filter pivoted at 1 kHz.  
Positive values boost highs and cut lows; negative values cut highs and boost lows.  
Useful for darkening or brightening the delay tail without external EQ.

### CHAOS

Micro-variation engine that adds organic randomness to the effect. Two independent chaos targets:

- **CHAOS F (Filter)**: Modulates the HP/LP filter cutoff frequencies when filters are enabled. Creates evolving tonal movement in the delay tail.
- **CHAOS D (Delay)**: Modulates the delay time. Produces drifting, tape-like pitch wobble.

Each chaos target has its own toggle and shares two global controls:

- **AMOUNT (0–100%)**: Modulation depth — how far from the base value the parameter can drift. Default: 50%.
- **SPEED (0.01–100 Hz)**: Sample-and-hold rate — how often a new random target is picked. Default: 5 Hz.

Uses exponential smoothing between random targets for glitch-free transitions.

### LIM THRESHOLD (−36 to 0 dB)

Peak limiter threshold. Sets the ceiling above which the limiter engages.
At 0 dB (default) the limiter acts as a transparent safety net. Lower values compress the signal harder.

### LIM MODE

Limiter insertion point:
- **NONE**: Limiter disabled.
- **WET**: Limiter applied to the wet signal only (after processing, before dry/wet mix).
- **GLOBAL**: Limiter applied to the final output (after output gain and dry/wet mix).

The limiter is a dual-stage transparent peak limiter:
- **Stage 1 (Leveler)**: 2 ms attack, 10 ms release — catches sustained overs.
- **Stage 2 (Brickwall)**: Instant attack, 100 ms release — catches transient peaks.

Stereo-linked gain reduction ensures consistent imaging.

## Technical Details

### DSP Architecture
- **Buffer**: Power-of-2 circular buffer with bitwise AND wrapping.
- **Interpolation**: 4-point Hermite cubic on all delay reads.
- **Smoothing**: One-pole EMA per sample for delay time, gain, and mix.
- **Feedback path**: DC blocker only (one-pole HP at 5 Hz). Sign-preserving bipolar smoothstep mapping. No saturation, no filtering.
- **Reverse taper**: Precomputed 129-point Tukey (raised-cosine) lookup table with linear interpolation. No per-sample trigonometry.
- **Wet filter**: Biquad HP/LP on the wet signal. Transposed Direct Form II. Coefficients updated once per block (channel 0), shared across channels.
- **Tilt EQ**: First-order symmetric shelf at 1 kHz. Coefficients cached with tolerance-based update.
- **Chaos**: Sample-and-hold random modulation with exponential smoothing. Per-block coefficient precomputation.

### MIDI Implementation
- Standard A440 tuning: `frequency = 440 × 2^((note − 69) / 12)`.
- Monophonic last-note priority. Note-off falls back to manual TIME knob.
- Channel filtering: OMNI (0) or specific channel (1–16).
- Priority: MIDI > SYNC > Manual TIME.

### State Persistence
- All parameters saved via JUCE AudioProcessorValueTreeState.
- UI state (size, palette, CRT toggle, MIDI channel, IO section expanded/collapsed) persisted in the plugin state.
- Parameter IDs are stable across versions for preset compatibility.

### Performance
- Zero-allocation audio thread. All buffers pre-allocated in `prepareToPlay`.
- Lock-free atomic parameter reads (`std::memory_order_relaxed`).
- Fast dB→linear conversion via `std::exp2` (single SSE instruction).
- Gain/mix smoothing snaps to target when within ε to avoid useless EMA in steady state.
- Performance tracing available via `ECHOTR_PERF_TRACE=1` compile flag (disabled by default).

### Build
- JUCE Framework, C++17, VST3 format.
- Visual Studio 2022 (MSBuild, x64 Release).
- Dependencies: JUCE modules only (no third-party libraries).

## Changelog

### v1.4
- Feedback is now bipolar (−100% to +100%). Negative feedback inverts polarity, producing pitch-inverted repetitions and alternate comb-filter character.
- Added ENGINE selector: CLEAN (default), TAPE, and BBD delay character modes.
- Added TILT EQ (−6 to +6 dB) — first-order spectral tilt on the wet signal.
- Added CHAOS engine with two independent targets: CHAOS F (filter modulation) and CHAOS D (delay time modulation). Sample-and-hold with exponential smoothing.
- Added safety hard-limiter at +48 dBFS on all output paths (forward and reverse). Catches NaN/Inf runaways without ever engaging during normal operation.
- INPUT slider now displays "−INF" when set to −80 dB or below.
- Numeric entry popup for percentage sliders: precision standardized to 1 decimal place.
- Filter coefficient update now uses tolerance-based comparison, preventing unnecessary recalculation from floating-point noise.
- Ported `drawToggleButton` with automatic text-shrinking from CAB-TR for consistent toggle rendering.
- Added dual-stage transparent peak limiter with LIM THRESHOLD (−36 to 0 dB) and LIM MODE (NONE/WET/GLOBAL). Stereo-linked gain reduction with 2 ms/10 ms leveler + instant/100 ms brickwall stages.
