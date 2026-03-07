#!/usr/bin/env python3
"""
ECHO-TR — Reverse pitch analysis
1. Current varispeed implementation (digital character)
2. Alternative: chunk-size modulation (musical grain texture)
3. TAU pitch-scaling behavior across delay times
"""

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from pathlib import Path

OUT = Path(__file__).parent
SR = 48000

# ─── Graph 1: Varispeed — how pitchRate changes chunk timing ───

fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle("CURRENT: Varispeed Pitch Shift in Reverse Mode", fontsize=14, fontweight='bold')

# Panel A: pitchRate vs semitones
semitones = np.linspace(-12, 12, 200)
pitch_rates = 2.0 ** (semitones / 12.0)

ax = axes[0, 0]
ax.plot(semitones, pitch_rates, 'b-', linewidth=2)
ax.axhline(1.0, color='gray', linestyle='--', alpha=0.5)
ax.axvline(0.0, color='gray', linestyle='--', alpha=0.5)
ax.set_xlabel("Semitones")
ax.set_ylabel("pitchRate (playback speed)")
ax.set_title("A) Read Speed vs Pitch Slider")
ax.grid(True, alpha=0.3)
ax.set_xlim(-12, 12)

# Panel B: chunk duration vs semitones for a fixed delay
delay_ms = 500  # 500ms delay
delay_smp = delay_ms * SR / 1000

ax = axes[0, 1]
chunk_durations = delay_smp / (pitch_rates * SR) * 1000  # ms
ax.plot(semitones, chunk_durations, 'r-', linewidth=2)
ax.axhline(delay_ms, color='gray', linestyle='--', alpha=0.5, label=f"Delay = {delay_ms} ms")
ax.set_xlabel("Semitones")
ax.set_ylabel("Chunk playback duration (ms)")
ax.set_title(f"B) Real-time Duration of Each Chunk (delay={delay_ms}ms)")
ax.legend()
ax.grid(True, alpha=0.3)
ax.set_xlim(-12, 12)

# Panel C: why it sounds digital — spectral smearing diagram
ax = axes[1, 0]
t = np.linspace(0, 1, 1000)
# Original 440 Hz sine
original = np.sin(2 * np.pi * 440 * t)
# Varispeed at +7 semitones (pitchRate ≈ 1.498)
rate = 2.0 ** (7/12)
t_stretched = t * rate
# Wrap around to simulate chunk reading
t_stretched_mod = t_stretched % 1.0
shifted = np.sin(2 * np.pi * 440 * t_stretched_mod)

ax.plot(t[:500], original[:500], 'b-', alpha=0.5, label="Original 440 Hz")
ax.plot(t[:500], shifted[:500], 'r-', alpha=0.5, label=f"+7st → {440*rate:.0f} Hz")
ax.set_xlabel("Time (normalized)")
ax.set_ylabel("Amplitude")
ax.set_title("C) Varispeed: Pitch is Literally Shifted")
ax.legend()
ax.grid(True, alpha=0.3)
ax.text(0.5, -0.85, "Every harmonic shifts → metallic/digital character\nat non-integer ratios",
        ha='center', fontsize=9, color='red', style='italic')

# Panel D: problem summary
ax = axes[1, 1]
ax.axis('off')
problems = [
    "PROBLEMAS del varispeed actual:",
    "",
    "1. Cambio literal de pitch → suena como sampler",
    "   (toda la señal se transpone, incluidos ruidos)",
    "",
    "2. Relaciones no-armónicas crean batido/disonancia",
    "   (+7 st = ×1.498 → todos los armónicos se desplazan)",
    "",
    "3. Chunk boundaries + pitch shift = artefactos",
    "   (fade cruzado no puede enmascarar el salto de pitch)",
    "",
    "4. No parece un efecto musical conocido sino",
    '   un "buffer manipulation" digital',
]
ax.text(0.05, 0.95, "\n".join(problems), transform=ax.transAxes,
        fontsize=10, verticalalignment='top', fontfamily='monospace',
        bbox=dict(boxstyle='round', facecolor='lightyellow', alpha=0.8))

plt.tight_layout()
fig.savefig(OUT / "rev_pitch_1_current.png", dpi=150)
plt.close()
print("✓ rev_pitch_1_current.png")


# ─── Graph 2: Alternative — Chunk Size Modulation ───

fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle("ALTERNATIVA: Chunk-Size Modulation (Reverse Grain Texture)", fontsize=14, fontweight='bold')

# Panel A: chunk multiplier curve
# Repurpose the -12..+12 range as a chunk multiplier
# 0 = 1.0x (normal), -12 = 0.125x (8× shorter), +12 = 4.0x (4× longer)
slider_vals = np.linspace(-12, 12, 200)
# Exponential mapping: 2^(val/6) gives:
#   -12 → 2^(-2) = 0.25x,  -6 → 0.5x,  0 → 1.0x,  +6 → 2.0x,  +12 → 4.0x
chunk_mult = 2.0 ** (slider_vals / 6.0)

ax = axes[0, 0]
ax.plot(slider_vals, chunk_mult, 'g-', linewidth=2)
ax.axhline(1.0, color='gray', linestyle='--', alpha=0.5, label="Normal (1×)")
ax.set_xlabel("Slider value")
ax.set_ylabel("Chunk multiplier")
ax.set_title("A) Chunk Size Multiplier vs Slider")
ax.legend()
ax.grid(True, alpha=0.3)

# Panel B: grain repetition rate
delay_ms_vals = [100, 250, 500, 1000]
ax = axes[0, 1]
for d_ms in delay_ms_vals:
    chunk_ms = d_ms * chunk_mult
    rep_rate = 1000.0 / chunk_ms  # chunks per second
    ax.plot(slider_vals, rep_rate, linewidth=1.5, label=f"Delay={d_ms}ms")
ax.set_xlabel("Slider value")
ax.set_ylabel("Chunk repetitions / sec")
ax.set_title("B) Grain Repetition Rate")
ax.legend()
ax.grid(True, alpha=0.3)
ax.set_ylim(0, 30)

# Panel C: visual representation of chunks
ax = axes[1, 0]
delay_smp_ex = 500 * SR // 1000  # 500 ms in samples
t_axis = np.arange(0, 3 * delay_smp_ex) / SR * 1000  # ms

multipliers = [0.25, 0.5, 1.0, 2.0]
colors = ['red', 'orange', 'green', 'blue']
for idx, (mult, col) in enumerate(zip(multipliers, colors)):
    chunk_smp = int(delay_smp_ex * mult)
    y_offset = idx * 1.5
    n_chunks = len(t_axis) // chunk_smp + 1
    for c in range(n_chunks):
        start = c * chunk_smp
        end = min(start + chunk_smp, len(t_axis))
        if start >= len(t_axis):
            break
        t_chunk = t_axis[start:end]
        # Triangle wave to represent reversed audio direction
        chunk_t = np.linspace(1, 0, end - start)
        ax.plot(t_chunk, chunk_t + y_offset, color=col, linewidth=1)
    ax.text(t_axis[-1] + 20, y_offset + 0.5, f"×{mult}", color=col, fontweight='bold')

ax.set_xlabel("Time (ms)")
ax.set_ylabel("Chunk read position (reversed)")
ax.set_title(f"C) Chunk Pattern (delay=500ms)")
ax.set_yticks([])
ax.grid(True, alpha=0.2)

# Panel D: comparison summary
ax = axes[1, 1]
ax.axis('off')
comparison = [
    "CHUNK-SIZE vs VARISPEED:",
    "",
    "  Varispeed (actual):           Chunk-Size (propuesta):",
    "  ─────────────────            ──────────────────────",
    "  Cambia PITCH percibido       Cambia TEXTURA granular",
    "  Suena digital/sampler        Suena granular/orgánico",
    "  Artefactos en boundaries     Taper suaviza transiciones",
    "  No tiene referente musical    ≈ granular reverse (Clouds)",
    "",
    "  El chunk-size NO cambia       A ×0.25: stuttery/glitchy",
    "  el pitch — solo la            A ×1.0: reverse normal",
    "  'granularidad' del reverse    A ×4.0: largo, flowing reverse",
    "",
    "  Se puede renombrar: 'GRAIN' en vez de 'PITCH'",
]
ax.text(0.02, 0.98, "\n".join(comparison), transform=ax.transAxes,
        fontsize=9, verticalalignment='top', fontfamily='monospace',
        bbox=dict(boxstyle='round', facecolor='lightcyan', alpha=0.8))

plt.tight_layout()
fig.savefig(OUT / "rev_pitch_2_alternative.png", dpi=150)
plt.close()
print("✓ rev_pitch_2_alternative.png")


# ─── Graph 3: TAU pitch-scaling behavior ───

fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle("TAU Pitch-Scaling: Comportamiento por Nota/Delay", fontsize=14, fontweight='bold')

kTauFloor = 0.030
kTauCeil = 3.000
kRefMs = 1000.0

# Panel A: pitchScale vs delay
delay_range_ms = np.linspace(10, 2000, 500)
pitch_scale = np.sqrt(np.clip(delay_range_ms / kRefMs, 0.001, 1.0))

ax = axes[0, 0]
ax.plot(delay_range_ms, pitch_scale, 'b-', linewidth=2)
ax.axhline(1.0, color='gray', linestyle='--', alpha=0.3)
ax.axvline(kRefMs, color='gray', linestyle='--', alpha=0.3, label=f"Referencia = {kRefMs:.0f} ms")
ax.set_xlabel("Delay (ms)")
ax.set_ylabel("pitchScale = sqrt(delay / ref)")
ax.set_title("A) Factor de Escala por Delay")
ax.legend()
ax.grid(True, alpha=0.3)
# Mark notes
notes = {
    "C2 (65Hz)": 1000/65*1000/SR*SR,  # just use ms directly
    "C3 (131Hz)": 1000/131*1000,
    "C4 (262Hz)": 1000/262*1000,
    "C5 (523Hz)": 1000/523*1000,
}
# Actually let's use MIDI delay times more meaningfully
# For a delay plugin, delay time comes from MIDI note period
midi_notes = {
    "C2 ~15.4ms": 15.4,
    "C3 ~7.6ms": 7.6,
    "C4 ~3.8ms": 3.8,
    "500ms": 500,
    "1000ms": 1000,
}
for label, d in midi_notes.items():
    ps = np.sqrt(min(d / kRefMs, 1.0))
    ax.annotate(f"{label}\n(×{ps:.2f})", (d, ps), fontsize=7,
                ha='center', va='bottom')

# Panel B: effective TAU for different TAU% settings across delays
ax = axes[0, 1]
tau_pcts = [0.0, 0.25, 0.50, 0.75, 1.0]
for tp in tau_pcts:
    tau_base = kTauFloor + (kTauCeil - kTauFloor) * tp
    ps = np.sqrt(np.clip(delay_range_ms / kRefMs, 0.001, 1.0))
    tau = kTauFloor + (tau_base - kTauFloor) * ps
    ax.plot(delay_range_ms, tau * 1000, linewidth=1.5, label=f"TAU={int(tp*100)}%")

ax.set_xlabel("Delay (ms)")
ax.set_ylabel("Effective tau (ms)")
ax.set_title("B) TAU Efectivo por Delay y Slider")
ax.legend()
ax.grid(True, alpha=0.3)
ax.set_ylim(0, 3500)

# Panel C: envelope recovery time (95% = 3×tau) for different delays
ax = axes[1, 0]
delays_test = [30, 50, 100, 250, 500, 1000]
tau_slider = np.linspace(0, 1, 200)

for d_ms in delays_test:
    tau_base = kTauFloor + (kTauCeil - kTauFloor) * tau_slider
    ps = np.sqrt(min(d_ms / kRefMs, 1.0))
    tau = kTauFloor + (tau_base - kTauFloor) * ps
    t95 = 3 * tau * 1000  # 95% recovery in ms
    ax.plot(tau_slider * 100, t95, linewidth=1.5, label=f"{d_ms}ms")

ax.set_xlabel("TAU slider (%)")
ax.set_ylabel("95% recovery (ms)")
ax.set_title("C) Tiempo de Recuperación 95% por Delay")
ax.legend()
ax.grid(True, alpha=0.3)

# Panel D: answer to user's question
ax = axes[1, 1]
ax.axis('off')
answer = [
    "RESPUESTA: ¿TAU aplica más o menos envelope en agudas?",
    "",
    "  pitchScale = sqrt(delay / 1000ms)",
    "",
    "  • Notas AGUDAS (delay corto, ej. 50ms):",
    "    pitchScale ≈ 0.22 → tau se ACORTA drásticamente",
    "    → El envelope se RECUPERA MUY RÁPIDO",
    "    → Feedback vuelve casi instantáneamente",
    "    → Efecto del auto-fbk apenas perceptible",
    "",
    "  • Notas GRAVES (delay largo, ej. 1000ms):",
    "    pitchScale = 1.0 → tau = valor completo del slider",
    "    → El envelope tarda más en recuperarse",
    "    → Efecto del auto-fbk claramente audible",
    "",
    "  IMPORTANTE: ATT (profundidad) NO cambia con pitch.",
    "  Solo TAU (velocidad de recuperación) se reduce.",
    "",
    "  En resumen: notas agudas → recuperación rápida",
    "              notas graves → recuperación lenta",
    "  (Esto imita cómo los ecos reales decaen más rápido",
    "   en frecuencias altas por absorción acústica.)",
]
ax.text(0.02, 0.98, "\n".join(answer), transform=ax.transAxes,
        fontsize=9, verticalalignment='top', fontfamily='monospace',
        bbox=dict(boxstyle='round', facecolor='lightyellow', alpha=0.8))

plt.tight_layout()
fig.savefig(OUT / "rev_pitch_3_tau_scaling.png", dpi=150)
plt.close()
print("✓ rev_pitch_3_tau_scaling.png")


# ─── Graph 4: Detailed chunk-size implementation proposal ───

fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle("Propuesta Detallada: GRAIN Control para Reverse", fontsize=14, fontweight='bold')

# Panel A: waveform simulation — normal reverse vs short grain
np.random.seed(42)
# Simulate a simple audio signal
t = np.linspace(0, 1.0, SR)
signal = np.sin(2 * np.pi * 220 * t) * 0.5
# Add some harmonics
signal += np.sin(2 * np.pi * 440 * t) * 0.3
signal += np.sin(2 * np.pi * 660 * t) * 0.1
# Envelope
env = np.exp(-3 * t)
signal *= env

ax = axes[0, 0]
# Normal reverse (chunk = delay = 500ms = 24000 samples)
chunk_normal = 24000
# Show first 48000 samples (1 second)
show_n = 48000
# Build reversed output for normal chunk
rev_normal = np.zeros(show_n)
for start in range(0, show_n, chunk_normal):
    end = min(start + chunk_normal, show_n)
    chunk = signal[start:end]
    rev_normal[start:end] = chunk[::-1][:end-start]

t_show = np.arange(show_n) / SR * 1000
ax.plot(t_show, rev_normal, 'g-', alpha=0.7, linewidth=0.5)
ax.set_title("A) Reverse Normal — chunk = delay (500ms)")
ax.set_xlabel("Time (ms)")
ax.set_ylabel("Amplitude")
ax.grid(True, alpha=0.2)

ax = axes[0, 1]
# Short grain (chunk = delay × 0.25 = 125ms = 6000 samples)
chunk_short = 6000
rev_short = np.zeros(show_n)
for start in range(0, show_n, chunk_short):
    end = min(start + chunk_short, show_n)
    chunk = signal[start:end]
    # Apply taper
    taper_len = min(128, len(chunk) // 4)
    win = np.ones(len(chunk))
    if taper_len > 0:
        win[:taper_len] = 0.5 * (1 - np.cos(np.pi * np.arange(taper_len) / taper_len))
        win[-taper_len:] = 0.5 * (1 - np.cos(np.pi * np.arange(taper_len, 0, -1) / taper_len))
    rev_short[start:end] = chunk[::-1][:end-start] * win[:end-start]

ax.plot(t_show, rev_short, 'r-', alpha=0.7, linewidth=0.5)
ax.set_title("B) Grain ×0.25 — chunk = 125ms (granular)")
ax.set_xlabel("Time (ms)")
ax.set_ylabel("Amplitude")
ax.grid(True, alpha=0.2)

# Panel C: slider mapping options
ax = axes[1, 0]
# Option 1: exponential 2^(val/6) — range 0.25 to 4.0
slider = np.linspace(-12, 12, 200)
opt_exp = 2.0 ** (slider / 6.0)
# Option 2: linear 0.125 to 4.0
opt_lin = np.interp(slider, [-12, 0, 12], [0.125, 1.0, 4.0])
# Option 3: symmetric log
opt_log2 = 2.0 ** (slider / 12.0)  # 0.5x to 2.0x

ax.plot(slider, opt_exp, 'b-', linewidth=2, label="2^(val/6): 0.25× – 4.0×")
ax.plot(slider, opt_log2, 'g--', linewidth=2, label="2^(val/12): 0.5× – 2.0×")
ax.axhline(1.0, color='gray', linestyle='--', alpha=0.5)
ax.set_xlabel("Slider value")
ax.set_ylabel("Chunk multiplier")
ax.set_title("C) Opciones de Mapeo del Slider")
ax.legend()
ax.grid(True, alpha=0.3)

# Panel D: summary
ax = axes[1, 1]
ax.axis('off')
summary = [
    "IMPLEMENTACIÓN PROPUESTA:",
    "",
    "  Cambio: Renombrar 'PITCH' → 'GRAIN'",
    "  Slider: -12 a +12 (misma UI)",
    "  Mapeo: chunkMult = 2^(slider / 6)",
    "",
    "  Slider  Mult   Efecto",
    "  ──────  ─────  ──────────────────────",
    "  -12     0.25×  Muy granular, stuttery",
    "   -6     0.50×  Granular moderado",
    "    0     1.00×  Reverse normal (actual)",
    "   +6     2.00×  Flowing, más continuo",
    "  +12     4.00×  Muy largo, tape-like",
    "",
    "  VENTAJAS:",
    "  • NO cambia pitch → más musical",
    "  • Carácter granular ≈ Clouds/Morphagene",
    "  • Taper existente suaviza boundaries",
    "  • Implementación simple: 1 línea cambia",
    "",
    "  Código: reverseChunkLen = delay × chunkMult",
    "  (en vez de: reverseCounter += pitchRate)",
]
ax.text(0.02, 0.98, "\n".join(summary), transform=ax.transAxes,
        fontsize=9, verticalalignment='top', fontfamily='monospace',
        bbox=dict(boxstyle='round', facecolor='lightgreen', alpha=0.8))

plt.tight_layout()
fig.savefig(OUT / "rev_pitch_4_proposal.png", dpi=150)
plt.close()
print("✓ rev_pitch_4_proposal.png")


# ─── Graph 5: Side-by-side comparison table ───

fig, ax = plt.subplots(figsize=(14, 6))
fig.suptitle("Comparación Final: Varispeed vs Chunk-Size", fontsize=14, fontweight='bold')
ax.axis('off')

table_data = [
    ["Slider=0", "pitchRate=1.0\nReverse normal", "chunkMult=1.0\nReverse normal"],
    ["+6", "pitchRate=1.41×\n+5ª justa percibida\nMás rápido, pitch UP", "chunkMult=2.0×\nGranos más largos\nMás flowing, SIN pitch"],
    ["+12", "pitchRate=2.0×\n+1 octava percibida\nDoble velocidad", "chunkMult=4.0×\nGranos muy largos\nContinuo, tape-like"],
    ["-6", "pitchRate=0.71×\n-5ª justa percibida\nMás lento, pitch DOWN", "chunkMult=0.5×\nGranos más cortos\nMás fragmentado"],
    ["-12", "pitchRate=0.5×\n-1 octava percibida\nMedia velocidad", "chunkMult=0.25×\nGranos muy cortos\nGlitchy/stuttery"],
]

table = ax.table(
    cellText=table_data,
    colLabels=["Valor", "VARISPEED (actual)\nCambia pitch", "CHUNK-SIZE (propuesta)\nCambia textura"],
    loc='center',
    cellLoc='center',
)
table.auto_set_font_size(False)
table.set_fontsize(9)
table.scale(1, 2.5)

# Style header
for j in range(3):
    cell = table[0, j]
    cell.set_facecolor('#4472C4')
    cell.set_text_props(color='white', fontweight='bold')

# Style rows
for i in range(1, 6):
    table[i, 0].set_facecolor('#D6E4F0')
    table[i, 1].set_facecolor('#FFE0E0')
    table[i, 2].set_facecolor('#E0FFE0')

plt.tight_layout()
fig.savefig(OUT / "rev_pitch_5_comparison.png", dpi=150)
plt.close()
print("✓ rev_pitch_5_comparison.png")

print("\n── DONE: 5 graphs generated ──")
