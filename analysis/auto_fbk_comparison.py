"""
Auto-feedback envelope analysis — AFTER improvements.
New constants: kTauFloor=30ms, kTauCeil=3s, pitch-scaling=sqrt(delay/ref), threshold=2%, cooldown=2048.
ATT is depth-only (no longer controls pitch-scaling exponent).
"""
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

SR = 48000.0

# ═══ NEW constants ═══
kTauFloor  = 0.030     # 30 ms (was 5 ms)
kTauCeil   = 3.000     # 3 s   (was 10 s)
kRefMs     = 1000.0    # reference delay for pitch-scaling

# ═══ OLD constants (for comparison) ═══
OLD_kTauFloor  = 0.005
OLD_kTauCeil   = 10.0
OLD_kRefMs     = 1000.0
OLD_kAttExpMax = 3.0

def compute_tau_NEW(tauPct, delayMs):
    """New: TAU sets base, pitch-scaling is fixed sqrt (ATT not involved)."""
    tauBase = kTauFloor + (kTauCeil - kTauFloor) * tauPct
    pitchScale = np.sqrt(np.clip(delayMs / kRefMs, 0.001, 1.0))
    return kTauFloor + (tauBase - kTauFloor) * pitchScale

def compute_tau_OLD(tauPct, attPct, delayMs):
    """Old: ATT controlled pitch-scaling exponent."""
    tauMax = OLD_kTauFloor + (OLD_kTauCeil - OLD_kTauFloor) * tauPct
    if attPct > 0.001:
        attExp = OLD_kAttExpMax * attPct
        ratio = np.clip(delayMs / OLD_kRefMs, 0.0, 1.0)
        shaped = ratio ** attExp
        return OLD_kTauFloor + (tauMax - OLD_kTauFloor) * shaped
    return tauMax

def envelope_curve(tau, attPct, duration_s=3.0):
    """Simulate envelope from closed-form."""
    N = int(SR * duration_s)
    envCoeff = np.exp(-1.0 / (SR * tau))
    n = np.arange(N, dtype=np.float64)
    env = 1.0 - envCoeff ** (n + 1)
    attCurved = 1.0 - (1.0 - attPct) ** 2
    envMix = 1.0 - attCurved * (1.0 - env)
    return n / SR, env, envMix


# ════════════════════════════════════════════════════════════════
# GRAPH 1: TAU sweep — NEW vs OLD at ATT=75%, delay=500ms
# ════════════════════════════════════════════════════════════════
print("Graph 1: TAU sweep comparison...", flush=True)
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

delayMs = 500.0; attPct = 0.75
for tauPct in [0.0, 0.25, 0.50, 0.75, 1.0]:
    tau = compute_tau_OLD(tauPct, attPct, delayMs)
    t, _, em = envelope_curve(tau, attPct, 5.0)
    ax1.plot(t, em, label=f'TAU={int(tauPct*100)}% (tau={tau*1000:.0f}ms)')
ax1.set_title(f'OLD: Feedback mult vs TAU (ATT=75%, delay={delayMs}ms)')
ax1.set_xlabel('Time (s)'); ax1.set_ylabel('Feedback multiplier')
ax1.set_ylim(-0.05, 1.05); ax1.legend(fontsize=8); ax1.grid(True, alpha=0.3)

for tauPct in [0.0, 0.25, 0.50, 0.75, 1.0]:
    tau = compute_tau_NEW(tauPct, delayMs)
    t, _, em = envelope_curve(tau, attPct, 5.0)
    ax2.plot(t, em, label=f'TAU={int(tauPct*100)}% (tau={tau*1000:.0f}ms)')
ax2.set_title(f'NEW: Feedback mult vs TAU (ATT=75%, delay={delayMs}ms)')
ax2.set_xlabel('Time (s)'); ax2.set_ylabel('Feedback multiplier')
ax2.set_ylim(-0.05, 1.05); ax2.legend(fontsize=8); ax2.grid(True, alpha=0.3)

fig.tight_layout(); fig.savefig('cmp1_tau_sweep.png', dpi=150); plt.close(fig)
print("  -> cmp1_tau_sweep.png", flush=True)


# ════════════════════════════════════════════════════════════════
# GRAPH 2: ATT sweep — NEW vs OLD at TAU=50%, delay=500ms
# ════════════════════════════════════════════════════════════════
print("Graph 2: ATT sweep comparison...", flush=True)
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

tauPct = 0.50; delayMs = 500.0
for attPct in [0.0, 0.10, 0.25, 0.50, 0.75, 1.0]:
    tau = compute_tau_OLD(tauPct, attPct, delayMs)
    t, _, em = envelope_curve(tau, attPct, 5.0)
    attC = 1.0 - (1.0 - attPct) ** 2
    ax1.plot(t, em, label=f'ATT={int(attPct*100)}% (tau={tau*1000:.0f}ms, attC={attC:.2f})')
ax1.set_title(f'OLD: ATT sweep (TAU=50%, delay={delayMs}ms)')
ax1.set_xlabel('Time (s)'); ax1.set_ylabel('Feedback multiplier')
ax1.set_ylim(-0.05, 1.05); ax1.legend(fontsize=7); ax1.grid(True, alpha=0.3)

for attPct in [0.0, 0.10, 0.25, 0.50, 0.75, 1.0]:
    tau = compute_tau_NEW(tauPct, delayMs)  # ATT no longer affects tau!
    t, _, em = envelope_curve(tau, attPct, 5.0)
    attC = 1.0 - (1.0 - attPct) ** 2
    ax2.plot(t, em, label=f'ATT={int(attPct*100)}% (tau={tau*1000:.0f}ms, attC={attC:.2f})')
ax2.set_title(f'NEW: ATT sweep — depth only (TAU=50%, delay={delayMs}ms)')
ax2.set_xlabel('Time (s)'); ax2.set_ylabel('Feedback multiplier')
ax2.set_ylim(-0.05, 1.05); ax2.legend(fontsize=7); ax2.grid(True, alpha=0.3)

fig.tight_layout(); fig.savefig('cmp2_att_sweep.png', dpi=150); plt.close(fig)
print("  -> cmp2_att_sweep.png", flush=True)


# ════════════════════════════════════════════════════════════════
# GRAPH 3: Delay length sweep — NEW vs OLD
# ════════════════════════════════════════════════════════════════
print("Graph 3: Delay length sweep comparison...", flush=True)
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

tauPct = 0.50; attPct = 0.75
for delayMs in [50, 100, 250, 500, 750, 1000]:
    tau = compute_tau_OLD(tauPct, attPct, delayMs)
    t, _, em = envelope_curve(tau, attPct, 5.0)
    ax1.plot(t, em, label=f'{delayMs}ms (tau={tau*1000:.0f}ms)')
ax1.set_title(f'OLD: Delay sweep (TAU=50%, ATT=75%)')
ax1.set_xlabel('Time (s)'); ax1.set_ylabel('Feedback multiplier')
ax1.set_ylim(-0.05, 1.05); ax1.legend(fontsize=8); ax1.grid(True, alpha=0.3)

for delayMs in [50, 100, 250, 500, 750, 1000]:
    tau = compute_tau_NEW(tauPct, delayMs)
    t, _, em = envelope_curve(tau, attPct, 5.0)
    ax2.plot(t, em, label=f'{delayMs}ms (tau={tau*1000:.0f}ms)')
ax2.set_title(f'NEW: Delay sweep — sqrt pitch-scaling (TAU=50%, ATT=75%)')
ax2.set_xlabel('Time (s)'); ax2.set_ylabel('Feedback multiplier')
ax2.set_ylim(-0.05, 1.05); ax2.legend(fontsize=8); ax2.grid(True, alpha=0.3)

fig.tight_layout(); fig.savefig('cmp3_delay_sweep.png', dpi=150); plt.close(fig)
print("  -> cmp3_delay_sweep.png", flush=True)


# ════════════════════════════════════════════════════════════════
# GRAPH 4: Effective tau surface — NEW vs OLD
# ════════════════════════════════════════════════════════════════
print("Graph 4: Tau surface comparison...", flush=True)
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

delays = [100, 250, 500, 1000]
tau_pcts = np.linspace(0, 1, 200)

for d in delays:
    taus = [compute_tau_OLD(tp, 0.75, d) * 1000 for tp in tau_pcts]
    ax1.plot(tau_pcts * 100, taus, label=f'{d}ms')
ax1.set_title('OLD: Effective tau (ATT=75%)')
ax1.set_xlabel('TAU (%)'); ax1.set_ylabel('Effective tau (ms)')
ax1.legend(); ax1.grid(True, alpha=0.3); ax1.set_yscale('log')

for d in delays:
    taus = [compute_tau_NEW(tp, d) * 1000 for tp in tau_pcts]
    ax2.plot(tau_pcts * 100, taus, label=f'{d}ms')
ax2.set_title('NEW: Effective tau (ATT irrelevant for tau)')
ax2.set_xlabel('TAU (%)'); ax2.set_ylabel('Effective tau (ms)')
ax2.legend(); ax2.grid(True, alpha=0.3); ax2.set_yscale('log')

fig.tight_layout(); fig.savefig('cmp4_tau_surface.png', dpi=150); plt.close(fig)
print("  -> cmp4_tau_surface.png", flush=True)


# ════════════════════════════════════════════════════════════════
# GRAPH 5: Short delays (MIDI high notes) — NEW behavior
# ════════════════════════════════════════════════════════════════
print("Graph 5: Short delays (MIDI)...", flush=True)
fig, ax = plt.subplots(figsize=(10, 6))
tauPct = 0.50; attPct = 0.75
for delayMs in [2, 5, 10, 20, 50, 100]:
    tau = compute_tau_NEW(tauPct, delayMs)
    t, _, em = envelope_curve(tau, attPct, 3.0)
    ax.plot(t, em, label=f'{delayMs}ms (tau={tau*1000:.1f}ms)')
ax.set_title(f'NEW: Short delays — sqrt scaling (TAU=50%, ATT=75%)')
ax.set_xlabel('Time (s)'); ax.set_ylabel('Feedback multiplier')
ax.set_ylim(-0.05, 1.05); ax.legend(fontsize=8); ax.grid(True, alpha=0.3)
fig.tight_layout(); fig.savefig('cmp5_short_delays.png', dpi=150); plt.close(fig)
print("  -> cmp5_short_delays.png", flush=True)


# ════════════════════════════════════════════════════════════════
# NUMERICAL TABLES
# ════════════════════════════════════════════════════════════════
print("\n" + "="*70)
print("NUMERICAL COMPARISON: OLD vs NEW")
print("="*70)

print(f"\n--- NEW effective tau (ms) at various delays ---")
print(f"{'TAU%':>6} {'50ms':>10} {'100ms':>10} {'250ms':>10} {'500ms':>10} {'1000ms':>10}")
for tp in [0, 10, 25, 50, 75, 100]:
    row = f"{tp:>6}"
    for d in [50, 100, 250, 500, 1000]:
        tau = compute_tau_NEW(tp/100, d)
        row += f"{tau*1000:>10.1f}"
    print(row)

print(f"\n--- OLD effective tau (ms) at ATT=75%, various delays ---")
print(f"{'TAU%':>6} {'50ms':>10} {'100ms':>10} {'250ms':>10} {'500ms':>10} {'1000ms':>10}")
for tp in [0, 10, 25, 50, 75, 100]:
    row = f"{tp:>6}"
    for d in [50, 100, 250, 500, 1000]:
        tau = compute_tau_OLD(tp/100, 0.75, d)
        row += f"{tau*1000:>10.1f}"
    print(row)

print(f"\n--- Time to 90% feedback (analytical) — NEW ---")
print(f"{'TAU%':>6} {'ATT=25%':>10} {'ATT=50%':>10} {'ATT=75%':>10} {'ATT=100%':>10}")
for tp in [0, 10, 25, 50, 75, 100]:
    row = f"{tp:>6}"
    for ap in [25, 50, 75, 100]:
        attC = 1.0 - (1.0 - ap/100) ** 2
        raw_needed = 1.0 - 0.10 / attC if attC > 1e-6 else 0.0
        tau = compute_tau_NEW(tp/100, 500.0)
        if 0 < raw_needed < 1:
            t_reach = -tau * np.log(1.0 - raw_needed)
        else:
            t_reach = float('inf')
        if t_reach < 100:
            row += f"{t_reach:>9.2f}s"
        else:
            row += f"{'inf':>10}"
    print(row)

print(f"\n--- Summary of improvements ---")
print(f"  TAU range:     {kTauFloor*1000:.0f}ms - {kTauCeil:.0f}s  (was {OLD_kTauFloor*1000:.0f}ms - {OLD_kTauCeil:.0f}s)")
print(f"  Pitch-scaling: sqrt(delay/ref) fixed  (was ATT-controlled pow(ratio, 3*ATT))")
print(f"  ATT role:      depth only  (was dual: depth + pitch-scaling)")
print(f"  Threshold:     2%  (was 0.5%)")
print(f"  Cooldown:      2048 samples (~43ms)  (was none)")

print("\nDONE.")
