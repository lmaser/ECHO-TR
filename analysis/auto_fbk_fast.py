"""
Auto-feedback envelope analysis for ECHO-TR — split into fast chunks.
"""
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import sys

SR = 48000.0
kTauFloor   = 0.005
kTauCeil    = 10.0
kRefMs      = 1000.0
kAttExpMax  = 3.0

def compute_tau(tauPct, attPct, delayMs):
    tauMax = kTauFloor + (kTauCeil - kTauFloor) * tauPct
    if attPct > 0.001:
        attExp = kAttExpMax * attPct
        ratio = np.clip(delayMs / kRefMs, 0.0, 1.0)
        shaped = ratio ** attExp
        tau = kTauFloor + (tauMax - kTauFloor) * shaped
    else:
        tau = tauMax
    return tau

def envelope_curve(tauPct, attPct, delayMs, duration_s=3.0):
    N = int(SR * duration_s)
    tau = compute_tau(tauPct, attPct, delayMs)
    envCoeff = np.exp(-1.0 / (SR * tau))
    n = np.arange(N, dtype=np.float64)
    env = 1.0 - envCoeff ** (n + 1)
    attCurved = 1.0 - (1.0 - attPct) ** 2
    envMix = 1.0 - attCurved * (1.0 - env)
    t = n / SR
    return t, env, envMix

print("Generating graph 1/8...", flush=True)
fig1, ax1 = plt.subplots(figsize=(10, 6))
delayMs = 500.0; attPct = 0.75
for tauPct in [0.0, 0.10, 0.25, 0.50, 0.75, 1.0]:
    t, _, em = envelope_curve(tauPct, attPct, delayMs, 5.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    ax1.plot(t, em, label=f'TAU={int(tauPct*100)}% (tau={tau_val*1000:.0f}ms)')
ax1.set_title(f'Feedback multiplier vs TAU (ATT=75%, delay={delayMs}ms)')
ax1.set_xlabel('Time (s)'); ax1.set_ylabel('Feedback multiplier')
ax1.set_ylim(-0.05, 1.05); ax1.legend(fontsize=8); ax1.grid(True, alpha=0.3)
fig1.tight_layout()
fig1.savefig('g1_tau_sweep.png', dpi=150)
plt.close(fig1)
print("  -> g1_tau_sweep.png", flush=True)

print("Generating graph 2/8...", flush=True)
fig2, ax2 = plt.subplots(figsize=(10, 6))
tauPct = 0.50; delayMs = 500.0
for attPct in [0.0, 0.10, 0.25, 0.50, 0.75, 1.0]:
    t, _, em = envelope_curve(tauPct, attPct, delayMs, 5.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    attCurved = 1.0 - (1.0 - attPct) ** 2
    ax2.plot(t, em, label=f'ATT={int(attPct*100)}% (tau={tau_val*1000:.0f}ms, attC={attCurved:.2f})')
ax2.set_title(f'Feedback multiplier vs ATT (TAU=50%, delay={delayMs}ms)')
ax2.set_xlabel('Time (s)'); ax2.set_ylabel('Feedback multiplier')
ax2.set_ylim(-0.05, 1.05); ax2.legend(fontsize=7); ax2.grid(True, alpha=0.3)
fig2.tight_layout()
fig2.savefig('g2_att_sweep.png', dpi=150)
plt.close(fig2)
print("  -> g2_att_sweep.png", flush=True)

print("Generating graph 3/8...", flush=True)
fig3, ax3 = plt.subplots(figsize=(10, 6))
tauPct = 0.50; attPct = 0.75
for delayMs in [50, 100, 250, 500, 750, 1000]:
    t, _, em = envelope_curve(tauPct, attPct, delayMs, 5.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    ax3.plot(t, em, label=f'delay={delayMs}ms (tau={tau_val*1000:.0f}ms)')
ax3.set_title(f'Feedback multiplier vs Delay Length (TAU=50%, ATT=75%)')
ax3.set_xlabel('Time (s)'); ax3.set_ylabel('Feedback multiplier')
ax3.set_ylim(-0.05, 1.05); ax3.legend(fontsize=8); ax3.grid(True, alpha=0.3)
fig3.tight_layout()
fig3.savefig('g3_delay_sweep.png', dpi=150)
plt.close(fig3)
print("  -> g3_delay_sweep.png", flush=True)

print("Generating graph 4/8...", flush=True)
fig4, ax4 = plt.subplots(figsize=(10, 6))
delayMs = 500.0
tau_pcts = np.linspace(0, 1, 200)
for attPct in [0.0, 0.25, 0.50, 0.75, 1.0]:
    taus_ms = np.array([compute_tau(tp, attPct, delayMs) * 1000 for tp in tau_pcts])
    ax4.plot(tau_pcts * 100, taus_ms, label=f'ATT={int(attPct*100)}%')
ax4.set_title(f'Effective tau (ms) vs TAU% (delay={delayMs}ms)')
ax4.set_xlabel('TAU (%)'); ax4.set_ylabel('Effective tau (ms)')
ax4.legend(); ax4.grid(True, alpha=0.3); ax4.set_yscale('log')
fig4.tight_layout()
fig4.savefig('g4_tau_surface.png', dpi=150)
plt.close(fig4)
print("  -> g4_tau_surface.png", flush=True)

print("Generating graph 5/8...", flush=True)
fig5, ax5 = plt.subplots(figsize=(10, 6))
for tauPct in [0.0, 0.25, 0.50, 0.75, 1.0]:
    t, _, em = envelope_curve(tauPct, 0.0, 500.0, 2.0)
    ax5.plot(t, em, label=f'TAU={int(tauPct*100)}%')
ax5.set_title('ATT=0%: Should be BYPASS (multiplier=1.0 always)')
ax5.set_xlabel('Time (s)'); ax5.set_ylabel('Feedback multiplier')
ax5.set_ylim(0.95, 1.05); ax5.legend(); ax5.grid(True, alpha=0.3)
fig5.tight_layout()
fig5.savefig('g5_att0_bypass.png', dpi=150)
plt.close(fig5)
print("  -> g5_att0_bypass.png", flush=True)

print("Generating graph 6/8...", flush=True)
fig6, ax6 = plt.subplots(figsize=(10, 6))
tauPct = 0.50; delayMs = 500.0
for attPct in [0.0, 0.01, 0.02, 0.05, 0.10, 0.25]:
    t, _, em = envelope_curve(tauPct, attPct, delayMs, 3.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    attCurved = 1.0 - (1.0 - attPct) ** 2
    ax6.plot(t, em, label=f'ATT={attPct*100:.0f}% (attC={attCurved:.3f}, tau={tau_val*1000:.0f}ms)')
ax6.set_title(f'Low ATT transition check (TAU=50%, delay={delayMs}ms)')
ax6.set_xlabel('Time (s)'); ax6.set_ylabel('Feedback multiplier')
ax6.set_ylim(-0.05, 1.05); ax6.legend(fontsize=7); ax6.grid(True, alpha=0.3)
fig6.tight_layout()
fig6.savefig('g6_low_att.png', dpi=150)
plt.close(fig6)
print("  -> g6_low_att.png", flush=True)

print("Generating graph 7/8...", flush=True)
fig7, ax7 = plt.subplots(figsize=(10, 6))
tauPct = 0.50; attPct = 0.75
for delayMs in [2, 5, 10, 20, 50, 100]:
    t, _, em = envelope_curve(tauPct, attPct, delayMs, 3.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    ax7.plot(t, em, label=f'delay={delayMs}ms (tau={tau_val*1000:.1f}ms)')
ax7.set_title(f'Very short delays (TAU=50%, ATT=75%)')
ax7.set_xlabel('Time (s)'); ax7.set_ylabel('Feedback multiplier')
ax7.set_ylim(-0.05, 1.05); ax7.legend(fontsize=8); ax7.grid(True, alpha=0.3)
fig7.tight_layout()
fig7.savefig('g7_short_delays.png', dpi=150)
plt.close(fig7)
print("  -> g7_short_delays.png", flush=True)

print("Generating graph 8/8...", flush=True)
fig8, axes8 = plt.subplots(1, 2, figsize=(16, 6))
# Left: ATT dual role
ax = axes8[0]
att_range = np.linspace(0, 1, 200)
attCurved = 1.0 - (1.0 - att_range) ** 2
ax.plot(att_range * 100, attCurved, 'b-', linewidth=2, label='Mod depth (attCurved)')
attExp = kAttExpMax * att_range
shaped_250 = (250.0 / kRefMs) ** attExp
shaped_500 = (500.0 / kRefMs) ** attExp
ax.plot(att_range * 100, shaped_250, 'r--', linewidth=2, label='Tau scale @ 250ms')
ax.plot(att_range * 100, shaped_500, 'g--', linewidth=2, label='Tau scale @ 500ms')
ax.set_title('ATT dual role: mod depth vs pitch-scaling')
ax.set_xlabel('ATT (%)'); ax.set_ylabel('Value (0-1)')
ax.legend(); ax.grid(True, alpha=0.3)

# Right: Time to 90% (analytical)
ax = axes8[1]
target = 0.90
for attPct in [0.25, 0.50, 0.75, 1.0]:
    times = []
    tau_range = np.linspace(0.01, 1, 100)
    attCurved_v = 1.0 - (1.0 - attPct) ** 2
    raw_needed = 1.0 - (1.0 - target) / attCurved_v if attCurved_v > 1e-6 else 0.0
    for tp in tau_range:
        tau = compute_tau(tp, attPct, 500.0)
        if raw_needed > 0 and raw_needed < 1:
            t_reach = -tau * np.log(1.0 - raw_needed)
        else:
            t_reach = 20.0
        times.append(min(t_reach, 20.0))
    ax.plot(tau_range * 100, times, label=f'ATT={int(attPct*100)}%')
ax.set_title(f'Time to {int(target*100)}% feedback (delay=500ms)')
ax.set_xlabel('TAU (%)'); ax.set_ylabel('Time (s)')
ax.legend(); ax.grid(True, alpha=0.3)
fig8.tight_layout()
fig8.savefig('g8_ux_analysis.png', dpi=150)
plt.close(fig8)
print("  -> g8_ux_analysis.png", flush=True)

# ── Numerical tables ──
print("\n" + "="*70)
print("NUMERICAL ANALYSIS")
print("="*70)

print(f"\n--- Effective tau (ms) at delay=500ms ---")
print(f"{'TAU%':>6} {'ATT=0%':>10} {'ATT=25%':>10} {'ATT=50%':>10} {'ATT=75%':>10} {'ATT=100%':>10}")
for tp in [0, 10, 25, 50, 75, 100]:
    row = f"{tp:>6}"
    for ap in [0, 25, 50, 75, 100]:
        tau = compute_tau(tp/100, ap/100, 500.0)
        row += f"{tau*1000:>10.1f}"
    print(row)

print(f"\n--- Effective tau (ms) at delay=100ms ---")
print(f"{'TAU%':>6} {'ATT=0%':>10} {'ATT=25%':>10} {'ATT=50%':>10} {'ATT=75%':>10} {'ATT=100%':>10}")
for tp in [0, 10, 25, 50, 75, 100]:
    row = f"{tp:>6}"
    for ap in [0, 25, 50, 75, 100]:
        tau = compute_tau(tp/100, ap/100, 100.0)
        row += f"{tau*1000:>10.1f}"
    print(row)

print(f"\n--- Effective tau (ms) at delay=50ms (high MIDI note) ---")
print(f"{'TAU%':>6} {'ATT=0%':>10} {'ATT=25%':>10} {'ATT=50%':>10} {'ATT=75%':>10} {'ATT=100%':>10}")
for tp in [0, 10, 25, 50, 75, 100]:
    row = f"{tp:>6}"
    for ap in [0, 25, 50, 75, 100]:
        tau = compute_tau(tp/100, ap/100, 50.0)
        row += f"{tau*1000:>10.1f}"
    print(row)

print(f"\n--- attCurved (quadratic modulation depth) ---")
for ap in [0, 1, 2, 5, 10, 25, 50, 75, 100]:
    attC = 1.0 - (1.0 - ap/100) ** 2
    print(f"  ATT={ap:>3}% -> attCurved={attC:.4f} -> at env=0: feedback_mult = {1.0 - attC:.4f}")

print(f"\n--- Time to 90% feedback recovery (delay=500ms) ---")
print(f"{'TAU%':>6} {'ATT=25%':>10} {'ATT=50%':>10} {'ATT=75%':>10} {'ATT=100%':>10}")
for tp in [0, 10, 25, 50, 75, 100]:
    row = f"{tp:>6}"
    for ap in [25, 50, 75, 100]:
        attC = 1.0 - (1.0 - ap/100) ** 2
        raw_needed = 1.0 - 0.10 / attC if attC > 1e-6 else 0.0
        tau = compute_tau(tp/100, ap/100, 500.0)
        if 0 < raw_needed < 1:
            t_reach = -tau * np.log(1.0 - raw_needed)
        else:
            t_reach = float('inf')
        row += f"{t_reach:>10.2f}s" if t_reach < 100 else f"{'inf':>10}"
    print(row)

print("\nDONE.")
