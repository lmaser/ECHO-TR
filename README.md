# ECHO-TR v1.0c

<br/><br/>

<img width="840" height="602" alt="image" src="https://github.com/user-attachments/assets/18c66779-09e8-4cac-995c-f174839efc2d" />

<br/><br/>

ECHO-TR is a creative delay effect built for texture generation, tonal manipulation, and pitch-shifted harmonics.  
It combines forward and reverse delay with MIDI-controlled pitch, auto-feedback dynamics, and a minimal CRT-inspired interface.

## Concept

ECHO-TR treats delay not as a mixing utility but as an instrument. By feeding MIDI notes into the delay engine, the delay time maps directly to pitch — turning the feedback loop into a resonator that can play melodies, drones, and evolving textures.

The reverse mode reads audio backward in chunks while keeping the feedback path forward and coherent. This means reverse tails behave identically to direct mode — only the output is reversed.

Auto-feedback adds an envelope that resets on every note change, letting the feedback "swell" back in naturally. The result is a self-clearing delay that never muddies across pitch changes.

## Interface

ECHO-TR uses a text-based UI with horizontal bar sliders. All controls are visible at once — no pages, tabs, or hidden menus.

- **Bar sliders**: Click and drag horizontally. Right-click for numeric entry (except STYLE, which is slider-only).
- **Toggle buttons**: SYNC, MIDI, AUTO FBK, RVS (reverse). Click to enable/disable.
- **Sub-labels**: Click the text next to MIDI, AUTO FBK, or RVS to open their configuration prompt.
- **Gear icon** (top-right): Opens the info popup with version, credits, and a link to Graphics settings.
- **Graphics popup**: Toggle CRT post-processing effect and switch between default/custom colour palettes.
- **Resize**: Drag the bottom-right corner. Size persists across sessions.

The value column to the right of each slider shows the current state in context:
- TIME shows milliseconds (or MIDI note name when active, or sync division).
- FEEDBACK shows percentage + "FBK".
- STYLE shows MONO/STEREO/WIDE/PING-PONG.
- MOD shows the frequency multiplier.
- INPUT/OUTPUT show dB values.
- MIX shows percentage.

## Parameters

### TIME (0–5000 ms)

Manual delay time. Overridden by MIDI or SYNC when active.  
Smoothed per-sample via exponential moving average (80 ms time constant) for glitch-free pitch sweeps.

When MIDI is active, TIME shows the note name instead of milliseconds. When the note releases, the delay glides back to the manual TIME knob value.

### FEEDBACK (0–100%)

Signal fed back into the delay line. 100% = infinite sustain / self-oscillation.  
Smoothstep mapping (3x²−2x³): the slider is linear (50% centred → 50% real), but both extremes have finer resolution — especially near 100% where self-oscillation lives. 90% slider → 97.2% real; 10% → 2.8%.  
Only a DC blocker (5 Hz high-pass) sits in the feedback path — no filtering, no saturation. Maximally transparent.

### STYLE

Routing topology for the delay:
- **MONO**: Single delay line, summed to both channels.
- **STEREO**: Independent left/right delay lines.
- **WIDE**: Cross-feedback between channels (like PING-PONG) but both channels receive their own stereo input. Creates a widening effect where repetitions gradually spread across the stereo field while preserving the original stereo image.
- **PING-PONG**: Cross-feedback with mono-summed input fed into the left channel only. Each repetition alternates fully between left and right.

All three modes share the same interpolation (4-point Hermite) and feedback processing.

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

### AUTO FBK

Automatic feedback envelope. When enabled, feedback resets to zero on every note/time/MOD change, then ramps back to the user's feedback setting.

This prevents muddy buildup between pitch changes while preserving self-oscillation during sustained notes.

**TAU (0–100%)**: Recovery speed.  
0% = fast recovery (30 ms), 100% = slow dramatic swell (3 s).  
Automatic pitch-scaling: shorter delays recover faster (`√(delay/1000)`).

**ATT (0–100%)**: Modulation depth.  
0% = envelope bypassed (feedback always at full), 100% = maximum suppression on reset.  
Cubic curve ensures gradual onset. UI 100% maps to internal 75% to keep the full range usable.

### RVS (Reverse)

Reverse delay mode. Reads audio backward in chunks, producing reversed playback.

The feedback path reads **forward** (identical to direct mode), so the delay buffer always contains coherent audio. Only the output is reversed. This means:
- Tails behave the same as direct mode.
- Self-oscillation works naturally.
- Switching between modes doesn't corrupt the buffer.

Each chunk's length equals the current (smoothed) delay time. At chunk boundaries, the next chunk adopts the latest delay value, providing natural pitch tracking.

**SMOOTH (−2 to +2)**: Controls the output taper at chunk edges.  
Maps to a multiplier: 2^(value).  
- −2 = ×0.25 (very short taper → choppy, granular).
- 0 = ×1.0 (clean default).
- +2 = ×4.0 (long taper → ambient, washy).

The taper is **proportional** to chunk length (1/16th × multiplier) so high MIDI notes (short chunks) are never silenced by a fixed-length taper.

## Technical Details

### DSP Architecture
- **Buffer**: Power-of-2 circular buffer with bitwise AND wrapping.
- **Interpolation**: 4-point Hermite cubic on all delay reads.
- **Smoothing**: One-pole EMA per sample for delay time, gain, and mix.
- **Feedback path**: DC blocker only (one-pole HP at 5 Hz). No saturation, no filtering.
- **Reverse taper**: Precomputed 129-point Tukey (raised-cosine) lookup table with linear interpolation. No per-sample trigonometry.

### MIDI Implementation
- Standard A440 tuning: `frequency = 440 × 2^((note − 69) / 12)`.
- Monophonic last-note priority. Note-off falls back to manual TIME knob.
- Channel filtering: OMNI (0) or specific channel (1–16).
- Priority: MIDI > SYNC > Manual TIME.

### State Persistence
- All parameters saved via JUCE AudioProcessorValueTreeState.
- UI state (size, palette, CRT toggle, MIDI channel) persisted in the plugin state.
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
