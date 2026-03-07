"""
Auto-feedback envelope analysis for ECHO-TR.
Simulates the exact C++ logic to visualize TAU/ATT behavior.
"""
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from pathlib import Path

OUT = Path(__file__).parent
SR = 48000.0

# Constants from C++
kTauFloor   = 0.005     # 5 ms
kTauCeil    = 10.0       # 10 s
kRefMs      = 1000.0     # 1 s reference
kAttExpMax  = 3.0

def compute_tau(tauPct, attPct, delayMs):
    """Exact replica of the C++ tau computation."""
    tauMax = kTauFloor + (kTauCeil - kTauFloor) * tauPct
    if attPct > 0.001:
        attExp = kAttExpMax * attPct
        ratio = np.clip(delayMs / kRefMs, 0.0, 1.0)
        shaped = ratio ** attExp
        tau = kTauFloor + (tauMax - kTauFloor) * shaped
    else:
        tau = tauMax
    return tau

def simulate_envelope(tauPct, attPct, delayMs, duration_s=3.0):
    """Simulate envelope from 0→1 after a reset, return time array and envelope."""
    N = int(SR * duration_s)
    tau = compute_tau(tauPct, attPct, delayMs)
    envCoeff = np.exp(-1.0 / (SR * tau))
    
    # Closed-form: env[n] = 1 - envCoeff^(n+1)
    n = np.arange(N)
    env = 1.0 - envCoeff ** (n + 1)
    
    # ATT modulation (quadratic curve)
    attCurved = 1.0 - (1.0 - attPct) ** 2
    envMix = 1.0 - attCurved * (1.0 - env)
    
    t = n / SR
    return t, env, envMix

# ════════════════════════════════════════════════════════════════
# GRAPH 1: Raw envelope (before ATT mix) for various TAU at fixed ATT=75%, delay=500ms
# ════════════════════════════════════════════════════════════════
fig, axes = plt.subplots(2, 2, figsize=(16, 12))

ax = axes[0, 0]
delayMs = 500.0
attPct = 0.75
for tauPct in [0.0, 0.10, 0.25, 0.50, 0.75, 1.0]:
    t, raw_env, env_mix = simulate_envelope(tauPct, attPct, delayMs, 5.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    ax.plot(t, env_mix, label=f'TAU={int(tauPct*100)}% (tau={tau_val*1000:.0f}ms)')
ax.set_title(f'Feedback multiplier vs TAU (ATT=75%, delay={delayMs}ms)')
ax.set_xlabel('Time (s)')
ax.set_ylabel('Feedback multiplier (envMix)')
ax.set_ylim(-0.05, 1.05)
ax.legend(fontsize=8)
ax.grid(True, alpha=0.3)
ax.axhline(y=0.95, color='red', linestyle='--', alpha=0.4, label='95% threshold')

# ════════════════════════════════════════════════════════════════
# GRAPH 2: Feedback multiplier for various ATT at fixed TAU=50%, delay=500ms
# ════════════════════════════════════════════════════════════════
ax = axes[0, 1]
tauPct = 0.50
delayMs = 500.0
for attPct in [0.0, 0.10, 0.25, 0.50, 0.75, 1.0]:
    t, raw_env, env_mix = simulate_envelope(tauPct, attPct, delayMs, 5.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    attCurved = 1.0 - (1.0 - attPct) ** 2
    ax.plot(t, env_mix, label=f'ATT={int(attPct*100)}% (tau={tau_val*1000:.0f}ms, attC={attCurved:.2f})')
ax.set_title(f'Feedback multiplier vs ATT (TAU=50%, delay={delayMs}ms)')
ax.set_xlabel('Time (s)')
ax.set_ylabel('Feedback multiplier (envMix)')
ax.set_ylim(-0.05, 1.05)
ax.legend(fontsize=7)
ax.grid(True, alpha=0.3)

# ════════════════════════════════════════════════════════════════
# GRAPH 3: Effect of delay length on recovery (ATT pitch-scaling)
# TAU=50%, ATT=75%
# ════════════════════════════════════════════════════════════════
ax = axes[1, 0]
tauPct = 0.50
attPct = 0.75
for delayMs in [50, 100, 250, 500, 750, 1000]:
    t, raw_env, env_mix = simulate_envelope(tauPct, attPct, delayMs, 5.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    ax.plot(t, env_mix, label=f'delay={delayMs}ms (tau={tau_val*1000:.0f}ms)')
ax.set_title(f'Feedback multiplier vs Delay Length (TAU=50%, ATT=75%)')
ax.set_xlabel('Time (s)')
ax.set_ylabel('Feedback multiplier (envMix)')
ax.set_ylim(-0.05, 1.05)
ax.legend(fontsize=8)
ax.grid(True, alpha=0.3)

# ════════════════════════════════════════════════════════════════
# GRAPH 4: TAU surface — effective tau (ms) as function of TAU% and ATT%
# at delay=500ms
# ════════════════════════════════════════════════════════════════
ax = axes[1, 1]
delayMs = 500.0
tau_pcts = np.linspace(0, 1, 100)
att_pcts = [0.0, 0.25, 0.50, 0.75, 1.0]
for attPct in att_pcts:
    taus_ms = [compute_tau(tp, attPct, delayMs) * 1000 for tp in tau_pcts]
    ax.plot(tau_pcts * 100, taus_ms, label=f'ATT={int(attPct*100)}%')
ax.set_title(f'Effective tau (ms) vs TAU% at various ATT (delay={delayMs}ms)')
ax.set_xlabel('TAU (%)')
ax.set_ylabel('Effective tau (ms)')
ax.legend()
ax.grid(True, alpha=0.3)
ax.set_yscale('log')

plt.tight_layout()
plt.savefig(str(OUT / 'auto_fbk_behavior.png'), dpi=150)
print(f"Saved: {OUT / 'auto_fbk_behavior.png'}")

# ════════════════════════════════════════════════════════════════
# GRAPH SET 2: Deeper analysis — discontinuities and edge cases
# ════════════════════════════════════════════════════════════════
fig2, axes2 = plt.subplots(2, 2, figsize=(16, 12))

# GRAPH 5: ATT=0% should be "imperceptible" — verify bypass
ax = axes2[0, 0]
for tauPct in [0.0, 0.25, 0.50, 0.75, 1.0]:
    t, raw_env, env_mix = simulate_envelope(tauPct, 0.0, 500.0, 2.0)
    ax.plot(t, env_mix, label=f'TAU={int(tauPct*100)}%')
ax.set_title('ATT=0%: Should be BYPASS (multiplier=1.0 always)')
ax.set_xlabel('Time (s)')
ax.set_ylabel('Feedback multiplier')
ax.set_ylim(0.95, 1.05)
ax.legend()
ax.grid(True, alpha=0.3)

# GRAPH 6: ATT near 0 (1%, 5%) — is there a jump?
ax = axes2[0, 1]
tauPct = 0.50
delayMs = 500.0
for attPct in [0.0, 0.01, 0.02, 0.05, 0.10, 0.25]:
    t, raw_env, env_mix = simulate_envelope(tauPct, attPct, delayMs, 3.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    attCurved = 1.0 - (1.0 - attPct) ** 2
    ax.plot(t, env_mix, label=f'ATT={attPct*100:.0f}% (attC={attCurved:.3f}, tau={tau_val*1000:.0f}ms)')
ax.set_title(f'Low ATT transition — discontinuity check (TAU=50%, delay={delayMs}ms)')
ax.set_xlabel('Time (s)')
ax.set_ylabel('Feedback multiplier')
ax.set_ylim(-0.05, 1.05)
ax.legend(fontsize=7)
ax.grid(True, alpha=0.3)

# GRAPH 7: Very short delays (MIDI high notes) — recovery speed
ax = axes2[1, 0]
tauPct = 0.50
attPct = 0.75
for delayMs in [2, 5, 10, 20, 50, 100]:
    t, raw_env, env_mix = simulate_envelope(tauPct, attPct, delayMs, 3.0)
    tau_val = compute_tau(tauPct, attPct, delayMs)
    ax.plot(t, env_mix, label=f'delay={delayMs}ms (tau={tau_val*1000:.1f}ms)')
ax.set_title(f'Very short delays / high MIDI (TAU=50%, ATT=75%)')
ax.set_xlabel('Time (s)')
ax.set_ylabel('Feedback multiplier')
ax.set_ylim(-0.05, 1.05)
ax.legend(fontsize=8)
ax.grid(True, alpha=0.3)

# GRAPH 8: The "shaped" curve — ratio^attExp for various ATT values
ax = axes2[1, 1]
delays = np.linspace(0, 1000, 500)
for attPct in [0.10, 0.25, 0.50, 0.75, 1.0]:
    attExp = kAttExpMax * attPct
    ratio = np.clip(delays / kRefMs, 0, 1)
    shaped = ratio ** attExp
    ax.plot(delays, shaped, label=f'ATT={int(attPct*100)}% (exp={attExp:.1f})')
ax.set_title('Shaped ratio: how delay length scales tau (ratio^attExp)')
ax.set_xlabel('Delay (ms)')
ax.set_ylabel('Shaped multiplier (0=fast recovery, 1=full tau)')
ax.legend()
ax.grid(True, alpha=0.3)

plt.tight_layout()
plt.savefig(str(OUT / 'auto_fbk_edge_cases.png'), dpi=150)
print(f"Saved: {OUT / 'auto_fbk_edge_cases.png'}")

# ════════════════════════════════════════════════════════════════
# GRAPH SET 3: The DUAL role of ATT — confusing UX
# ════════════════════════════════════════════════════════════════
fig3, axes3 = plt.subplots(1, 2, figsize=(16, 6))

# Left: ATT controls BOTH modulation depth AND pitch-scaling strength
# Show the two effects separately
ax = axes3[0]
att_range = np.linspace(0, 1, 200)
# Effect 1: attCurved (modulation depth)
attCurved = 1.0 - (1.0 - att_range) ** 2
ax.plot(att_range * 100, attCurved, 'b-', linewidth=2, label='Mod depth (attCurved)')
# Effect 2: attExp (pitch-scaling strength)  
attExp = kAttExpMax * att_range
# Show shaped ratio at delay=250ms as proxy
ratio_250 = 250.0 / kRefMs
shaped_250 = ratio_250 ** attExp
ax.plot(att_range * 100, shaped_250, 'r--', linewidth=2, label='Tau scale @ 250ms delay')
ratio_500 = 500.0 / kRefMs
shaped_500 = ratio_500 ** attExp
ax.plot(att_range * 100, shaped_500, 'g--', linewidth=2, label='Tau scale @ 500ms delay')
ax.set_title('ATT dual role: modulation depth vs pitch-scaling')
ax.set_xlabel('ATT (%)')
ax.set_ylabel('Value (0-1)')
ax.legend()
ax.grid(True, alpha=0.3)

# Right: What user ACTUALLY hears — time to 90% feedback at various combos
ax = axes3[1]
target_level = 0.90
results = {}
for attPct in [0.25, 0.50, 0.75, 1.0]:
    times = []
    tau_pcts_range = np.linspace(0.01, 1, 50)
    for tauPct in tau_pcts_range:
        # Analytical: time to reach target = -tau * ln(1 - target_raw)
        # where target_raw solves: 1 - attCurved*(1 - raw) = target_level
        attCurved = 1.0 - (1.0 - attPct) ** 2
        if attCurved < 1e-6:
            times.append(0.0)
            continue
        raw_needed = 1.0 - (1.0 - target_level) / attCurved
        if raw_needed >= 1.0 or raw_needed <= 0.0:
            times.append(20.0)
            continue
        tau = compute_tau(tauPct, attPct, 500.0)
        t_reach = -tau * np.log(1.0 - raw_needed)
        times.append(min(t_reach, 20.0))
    ax.plot(tau_pcts_range * 100, times, label=f'ATT={int(attPct*100)}%')
ax.set_title(f'Time to reach {int(target_level*100)}% feedback (delay=500ms)')
ax.set_xlabel('TAU (%)')
ax.set_ylabel('Time (s)')
ax.legend()
ax.grid(True, alpha=0.3)

plt.tight_layout()
plt.savefig(str(OUT / 'auto_fbk_ux_analysis.png'), dpi=150)
print(f"Saved: {OUT / 'auto_fbk_ux_analysis.png'}")

print("\n=== ANALYSIS COMPLETE ===")
print(f"Key findings:")
print(f"  kTauFloor = {kTauFloor*1000}ms, kTauCeil = {kTauCeil}s")
print(f"  kRefMs = {kRefMs}ms, kAttExpMax = {kAttExpMax}")

# Print a table of effective tau values
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

print(f"\n--- attCurved (quadratic) ---")
for ap in [0, 1, 2, 5, 10, 25, 50, 75, 100]:
    attC = 1.0 - (1.0 - ap/100) ** 2
    print(f"  ATT={ap:>3}% → attCurved={attC:.4f}")
