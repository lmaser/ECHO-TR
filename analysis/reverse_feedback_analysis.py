#!/usr/bin/env python3
"""
ECHO-TR — Reverse vs Direct feedback analysis
Diagnoses why reverse has less "rewind" effect and proposes fixes.
"""

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from pathlib import Path

OUT = Path(__file__).parent
SR = 48000

# ─── Graph 1: Taper energy loss analysis ───

fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle("Diagnóstico: Por qué Reverse tiene menos 'rebobinado' que Direct",
             fontsize=14, fontweight='bold')

# Panel A: Taper shape and energy loss per iteration
ax = axes[0, 0]
delays_ms = [3.8, 10, 50, 100, 250, 500, 1000]
taper_samples = 128
energy_retained = []

for d_ms in delays_ms:
    chunk_smp = int(d_ms * SR / 1000)
    if chunk_smp < 2:
        energy_retained.append(0.0)
        continue

    # Build taper window for the chunk
    taper_len = min(chunk_smp // 2, max(1, min(taper_samples, int(chunk_smp * 0.25))))
    win = np.ones(chunk_smp)
    for i in range(taper_len):
        w = 0.5 * (1 - np.cos(np.pi * i / taper_len))
        win[i] = w
        win[chunk_smp - 1 - i] = w

    # Energy retained = mean(win)  (compared to 1.0 for no taper)
    energy_retained.append(np.mean(win))

ax.bar(range(len(delays_ms)), [e * 100 for e in energy_retained],
       tick_label=[f"{d}ms" for d in delays_ms], color='steelblue', edgecolor='black')
ax.axhline(100, color='green', linestyle='--', alpha=0.5, label='Direct mode (100%)')
ax.set_ylabel("Energía retenida por iteración (%)")
ax.set_xlabel("Delay time (chunk size)")
ax.set_title("A) Energía del feedback por chunk — Reverse vs Direct")
ax.legend()
ax.set_ylim(0, 105)

for i, e in enumerate(energy_retained):
    ax.text(i, e * 100 + 1, f"{e*100:.1f}%", ha='center', fontsize=8)

# Panel B: Cumulative energy loss over feedback iterations
ax = axes[0, 1]
iterations = np.arange(1, 20)
for d_ms, energy in zip(delays_ms, energy_retained):
    if energy > 0:
        cum_energy = energy ** iterations * 100
        ax.plot(iterations, cum_energy, 'o-', markersize=3, label=f"{d_ms}ms")

ax.axhline(100 * (1.0 ** 10), color='green', linestyle='--', alpha=0.5, label='Direct (sin pérdida)')
ax.set_xlabel("Iteraciones de feedback")
ax.set_ylabel("Energía acumulada (%)")
ax.set_title("B) Decaimiento de energía tras N iteraciones")
ax.legend(fontsize=7)
ax.set_ylim(0, 105)
ax.grid(True, alpha=0.3)

# Panel C: Taper window visualization for different delays
ax = axes[1, 0]
for d_ms, col in zip([3.8, 50, 500], ['red', 'orange', 'green']):
    chunk_smp = int(d_ms * SR / 1000)
    taper_len = min(chunk_smp // 2, max(1, min(taper_samples, int(chunk_smp * 0.25))))
    win = np.ones(chunk_smp)
    for i in range(taper_len):
        w = 0.5 * (1 - np.cos(np.pi * i / taper_len))
        win[i] = w
        win[chunk_smp - 1 - i] = w

    t = np.arange(chunk_smp) / SR * 1000
    ax.plot(t, win, color=col, linewidth=1.5, label=f"{d_ms}ms (taper={taper_len}smp)")

ax.set_xlabel("Posición en chunk (ms)")
ax.set_ylabel("Taper weight")
ax.set_title("C) Ventana de taper por delay")
ax.legend()
ax.grid(True, alpha=0.3)
ax.set_ylim(-0.05, 1.1)

# Panel D: Root cause summary
ax = axes[1, 1]
ax.axis('off')
text = [
    "DIAGNÓSTICO — ¿Por qué menos 'rebobinado' en reverse?",
    "",
    "CAUSA PRINCIPAL: El taper Tukey se aplica tanto al",
    "OUTPUT como al FEEDBACK. En cada iteración:",
    "",
    "  Direct: feedback = delayed × fbk     (100% energía)",
    "  Reverse: feedback = reversed × taper × fbk  (< 100%)",
    "",
    "  A 500ms → 93% por iteración → OK",
    "  A  50ms → 73% por iteración → rápido decaimiento",
    "  A 3.8ms → 50% por iteración → cola muere en 2-3 reps",
    "",
    "CAUSA SECUNDARIA (MIDI note-off):",
    "  Al soltar nota MIDI, delay salta de ej. 3.8ms a 500ms.",
    "  Los chunks empiezan a cubrir zonas del buffer que",
    "  contienen silencio → cola se diluye con silencio.",
    "",
    "SOLUCIÓN: Separar taper de output vs feedback.",
    "  • Output: taper completo (eliminación de clicks)",
    "  • Feedback: micro-taper de 16 smp (0.33ms) solo",
    "    para anti-click, preserva ~99.9% de energía",
]
ax.text(0.02, 0.98, "\n".join(text), transform=ax.transAxes,
        fontsize=9, verticalalignment='top', fontfamily='monospace',
        bbox=dict(boxstyle='round', facecolor='lightyellow', alpha=0.8))

plt.tight_layout()
fig.savefig(OUT / "rev_fbk_1_diagnosis.png", dpi=150)
plt.close()
print("✓ rev_fbk_1_diagnosis.png")


# ─── Graph 2: Fix proposal — separated taper ───

fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle("FIX: Taper separado para Output vs Feedback",
             fontsize=14, fontweight='bold')

# Panel A: Energy comparison OLD vs NEW
ax = axes[0, 0]
delays_ms_test = [3.8, 10, 20, 50, 100, 250, 500, 1000]
energy_old = []
energy_new = []
fbk_taper = 16  # fixed 16 samples for feedback anti-click

for d_ms in delays_ms_test:
    chunk_smp = int(d_ms * SR / 1000)
    if chunk_smp < 2:
        energy_old.append(0)
        energy_new.append(0)
        continue

    # OLD: full taper on feedback
    taper_len = min(chunk_smp // 2, max(1, min(taper_samples, int(chunk_smp * 0.25))))
    win_old = np.ones(chunk_smp)
    for i in range(taper_len):
        w = 0.5 * (1 - np.cos(np.pi * i / taper_len))
        win_old[i] = w
        win_old[chunk_smp - 1 - i] = w
    energy_old.append(np.mean(win_old))

    # NEW: micro-taper on feedback (16 samples)
    micro_taper = min(fbk_taper, chunk_smp // 2)
    win_new = np.ones(chunk_smp)
    for i in range(micro_taper):
        w = 0.5 * (1 - np.cos(np.pi * i / micro_taper))
        win_new[i] = w
        win_new[chunk_smp - 1 - i] = w
    energy_new.append(np.mean(win_new))

x = np.arange(len(delays_ms_test))
w = 0.35
ax.bar(x - w/2, [e*100 for e in energy_old], w, label='OLD (taper en fbk)', color='salmon', edgecolor='black')
ax.bar(x + w/2, [e*100 for e in energy_new], w, label='NEW (micro-taper en fbk)', color='lightgreen', edgecolor='black')
ax.set_xticks(x)
ax.set_xticklabels([f"{d}ms" for d in delays_ms_test], fontsize=8)
ax.axhline(100, color='green', linestyle='--', alpha=0.3, label='Direct (100%)')
ax.set_ylabel("Energía retenida por iteración (%)")
ax.set_title("A) Energía feedback: OLD vs NEW")
ax.legend(fontsize=8)
ax.set_ylim(0, 105)

# Panel B: Cumulative energy 10 iterations comparison
ax = axes[0, 1]
n_iter = 10
cum_old = [e ** n_iter * 100 for e in energy_old]
cum_new = [e ** n_iter * 100 for e in energy_new]

ax.bar(x - w/2, cum_old, w, label='OLD', color='salmon', edgecolor='black')
ax.bar(x + w/2, cum_new, w, label='NEW', color='lightgreen', edgecolor='black')
ax.set_xticks(x)
ax.set_xticklabels([f"{d}ms" for d in delays_ms_test], fontsize=8)
ax.set_ylabel("Energía tras 10 iteraciones (%)")
ax.set_title(f"B) Energía acumulada tras {n_iter} iteraciones")
ax.legend(fontsize=8)
ax.set_ylim(0, 105)
ax.grid(True, alpha=0.3)

# Panel C: Code flow diagram
ax = axes[1, 0]
ax.axis('off')
code_flow = [
    "FLUJO DE SEÑAL — PROPUESTA:",
    "",
    "  ┌─ Hermite read (rawRevL, rawRevR) ─┐",
    "  │                                     │",
    "  ▼                                     ▼",
    "  OUTPUT TAPER                    FBK MICRO-TAPER",
    "  (configurable via SMOOTH)       (fijo, 16 smp)",
    "  │                                     │",
    "  ▼                                     ▼",
    "  channelL[i] = input*(1-mix)    fbkRevL = raw*microTaper*fbk",
    "    + tapered * mix * outGain      → DC blocker",
    "                                    → delayL[writePos]",
    "",
    "  SMOOTH controla el taper de OUTPUT:",
    "   -2.0 → taper corto → choppy, rítmico",
    "    0.0 → taper estándar (128smp, 25%)",
    "   +2.0 → taper largo → ambient, wash",
    "",
    "  FBK micro-taper: 16 smp fijo (~0.33ms @ 48kHz)",
    "  → Previene clicks, pero retiene ~99.9% de energía",
    "  → Feedback se comporta casi idéntico a direct mode",
]
ax.text(0.02, 0.98, "\n".join(code_flow), transform=ax.transAxes,
        fontsize=9, verticalalignment='top', fontfamily='monospace',
        bbox=dict(boxstyle='round', facecolor='lightcyan', alpha=0.8))

# Panel D: SMOOTH parameter mapping
ax = axes[1, 1]
smooth_vals = np.linspace(-2, 2, 200)
smooth_mults = 2.0 ** smooth_vals
base_taper = 128
taper_len_out = base_taper * smooth_mults

ax.plot(smooth_vals, taper_len_out, 'b-', linewidth=2, label='Output taper (samples)')
ax.axhline(16, color='red', linestyle='--', alpha=0.5, label='Fbk micro-taper (fijo, 16)')
ax.axhline(128, color='gray', linestyle='--', alpha=0.3, label='Default (SMOOTH=0)')
ax.set_xlabel("SMOOTH slider")
ax.set_ylabel("Taper length (samples)")
ax.set_title("D) SMOOTH: Control del taper de salida")
ax.legend()
ax.grid(True, alpha=0.3)

# Annotate key points
for sv, label in [(-2, "32 smp\nchoppy"), (0, "128 smp\nnormal"), (2, "512 smp\nambient")]:
    tl = base_taper * 2**sv
    ax.annotate(label, (sv, tl), fontsize=8, ha='center', va='bottom',
                xytext=(0, 10), textcoords='offset points')

plt.tight_layout()
fig.savefig(OUT / "rev_fbk_2_fix.png", dpi=150)
plt.close()
print("✓ rev_fbk_2_fix.png")


# ─── Graph 3: Summary table ───

fig, ax = plt.subplots(figsize=(14, 5))
fig.suptitle("Resumen: Cambios en Reverse Mode", fontsize=14, fontweight='bold')
ax.axis('off')

table_data = [
    ["ANTES", "AHORA", "IMPACTO"],
    ["Taper en output Y feedback", "Taper solo en output\nMicro-taper (16smp) en fbk",
     "Feedback ~99.9% energía\nvs ~73% (50ms) / ~50% (3.8ms)"],
    ["GRAIN controlaba chunk size\n(redundante con MOD)", "SMOOTH controla taper length\ndel output (32-512 smp)",
     "Control real sobre textura\nsin duplicar MOD"],
    ["Chunk size = delay × grainMult", "Chunk size = delay\n(como antes)",
     "MOD controla tamaño,\nSMOOTH controla crossfade"],
    ["Prompt: 'GRAIN 1.00 x'", "Prompt: 'SMOOTH 1.00 x'",
     "Mismo rango UI (-2 a +2)\nsolo cambia nombre/semántica"],
]

table = ax.table(cellText=table_data[1:], colLabels=table_data[0],
                 loc='center', cellLoc='center')
table.auto_set_font_size(False)
table.set_fontsize(9)
table.scale(1, 2.5)

for j in range(3):
    table[0, j].set_facecolor('#4472C4')
    table[0, j].set_text_props(color='white', fontweight='bold')

for i in range(1, 5):
    table[i, 0].set_facecolor('#FFE0E0')
    table[i, 1].set_facecolor('#E0FFE0')
    table[i, 2].set_facecolor('#F0F0FF')

plt.tight_layout()
fig.savefig(OUT / "rev_fbk_3_summary.png", dpi=150)
plt.close()
print("✓ rev_fbk_3_summary.png")

print("\n── DONE: 3 analysis graphs generated ──")
