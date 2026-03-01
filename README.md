# ECHO-TR v1.0b

<br/><br/>

<!-- Placeholder for screenshot -->
<!-- <img width="449" height="450" alt="image" src="" /> -->

<br/><br/>


ECHO-TR is a creative tape-style delay effect designed for texture generation, tonal manipulation, and pitch-shifted harmonics.  
Rather than conventional studio-grade delay for mixing, ECHO-TR focuses on experimental sound design—think Aphex Twin glitchy textures, Igorrr-style pitch madness, and evolving sonic landscapes.

## What it does

- Provides classic tape-delay behavior with variable delay time (1ms - 2000ms).
- Features three processing modes: **Stereo**, **Mono**, and **Ping-Pong**.
- Accepts **MIDI note input** to control delay time based on note frequency (delay time = 1/frequency period).
- Uses **exponential smoothing** to enable smooth pitch-shifting effects during time changes (no decimating artifacts).
- Supports **tempo sync** for rhythmic delay patterns aligned to DAW BPM.
- Includes **modulation** and **auto-feedback** for evolving, self-oscillating textures.

## Parameters

### Core Delay

- **TIME (ms)**
  - Manual delay time control (1ms - 2000ms).
  - Overridden when MIDI or SYNC modes are active.
  - Smoothed exponentially for glitch-free pitch shifting.

- **FEEDBACK**
  - Amount of delayed signal fed back into the delay line.
  - Higher values create longer, more resonant tails.
  - Can self-oscillate at extreme settings for drone/noise textures.

- **MIX**
  - Dry/wet balance.
  - 0% = fully dry, 100% = fully wet.

### Processing Modes

- **MODE**
  - **Stereo**: Independent L/R delay lines.
  - **Mono**: Single delay line summed to both channels.
  - **Ping-Pong**: Alternating L/R feedback for stereo bouncing effect.

### MIDI Control

- **MIDI**
  - Enables MIDI note input to control delay time.
  - When enabled, incoming MIDI notes set delay time to **1000ms / note_frequency**.
  - Example: C2 (130.81 Hz) → 7.64ms delay → creates pitch-shifted tones.
  - Priority: MIDI > SYNC > Manual TIME.

- **MIDI Port**
  - Selects MIDI channel (1-16) or Omni mode (0 = all channels).
  - State persists across sessions.

### Tempo Sync

- **SYNC**
  - Locks delay time to DAW tempo.
  - Provides musical subdivisions (1/4, 1/8, 1/16, dotted, triplets, etc.).
  - Active when MIDI is disabled.

### Modulation & Dynamics

- **MOD**
  - Modulation depth/rate (implementation-specific).
  - Adds movement and variation to delay characteristics.

- **AUTO FBK**
  - Automatic feedback control.
  - Dynamically adjusts feedback based on input/output levels.

### I/O Levels

- **INPUT**
  - Pre-delay gain staging.
  
- **OUTPUT**
  - Post-processing output level.

## Creative Use Cases

### Pitch-Shifted Harmonics
- Enable MIDI mode and play low notes (C1-C2) to generate audible delay times (15-30ms).
- Feedback creates stacked pitch-shifted layers → harmonic drones.
- Change notes in real-time for glitchy melodic textures.

### Comb Filtering
- Use very short delay times (0.5-5ms) with feedback.
- Creates metallic, resonant tones (comb filter effect).
- MIDI control allows "playing" the comb filter like an instrument.

### Tape Emulation
- Moderate delay times (50-500ms) with medium feedback.
- Stereo or Ping-Pong modes for classic tape echo flavor.
- Smoothing prevents tape-stop artifacts during time changes.

### Extreme Textures
- High feedback + modulation + auto-feedback.
- Self-oscillating delay lines create evolving noise/drone beds.
- Short MIDI-controlled times produce lo-fi bit-crushed chaos.

## Notes

- **Exponential smoothing** (coefficient 0.9997, ~330ms time constant) is applied per-sample to prevent decimating/aliasing during delay time changes.
- MIDI processing uses standard A440 tuning: `frequency = 440 * 2^((note-69)/12)`.
- File-based logging to `Desktop/ECHO-TR_MIDI_DEBUG.txt` for MIDI debugging (500 line limit, auto-wraps).
- UI state (width, height, palette, MIDI port) persists via APVTS state management.
- Graphics options available from gear icon (`Info -> Graphics`): toggle `TEXT FX` and `Custom Palette`.

## Technical Details

- Delay buffer allocation: power-of-2 sizing for efficient circular buffer indexing.
- Read position calculation uses bitwise AND masking: `(writePos - delaySamples) & (bufferLength - 1)`.
- MIDI channel filtering: `selectedPort=0` accepts all channels (omni), `1-16` filters to specific channel.
- Processing priority: MIDI note > Tempo Sync > Manual Time parameter.

## TODO

- [ ] Fine-tune smoothing coefficient and modulation parameters based on user testing.
- [ ] Optimize CPU usage for real-time performance under heavy feedback conditions.
- [ ] Consider adding visual feedback for MIDI note activity in GUI.
- [ ] Evaluate additional delay modes (reverse, granular, etc.).