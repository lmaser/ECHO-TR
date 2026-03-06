import csv, statistics

rows = []
with open(r'c:\Users\Dev\Desktop\echotr_perf_dump.csv') as f:
    reader = csv.DictReader(f)
    for r in reader:
        try:
            rows.append({
                'ts': float(r['timestamp_s']),
                'dur': float(r['duration_us']),
                'bs': int(r['block_size']),
                'cpu': float(r['cpu_percent'])
            })
        except:
            pass

durations = [r['dur'] for r in rows]
cpus = [r['cpu'] for r in rows]
blocks = [r['bs'] for r in rows]
sd = sorted(durations)
sc = sorted(cpus)

print(f"Total: {len(rows)} blocks, {rows[-1]['ts']-rows[0]['ts']:.0f}s")
print(f"\n--- Duration (us) ---")
print(f"  Mean:  {statistics.mean(durations):.1f}")
print(f"  Med:   {statistics.median(durations):.1f}")
print(f"  P95:   {sd[int(len(sd)*0.95)]:.1f}")
print(f"  P99:   {sd[int(len(sd)*0.99)]:.1f}")
print(f"  P99.9: {sd[int(len(sd)*0.999)]:.1f}")
print(f"  Max:   {max(durations):.1f}")

print(f"\n--- CPU % ---")
print(f"  Mean:  {statistics.mean(cpus):.3f}")
print(f"  Med:   {statistics.median(cpus):.3f}")
print(f"  P95:   {sc[int(len(sc)*0.95)]:.3f}")
print(f"  P99:   {sc[int(len(sc)*0.99)]:.3f}")
print(f"  P99.9: {sc[int(len(sc)*0.999)]:.3f}")
print(f"  Max:   {max(cpus):.3f}")

print(f"\n--- Block Size ---")
print(f"  Min: {min(blocks)}  Med: {statistics.median(blocks):.0f}  Max: {max(blocks)}")

# Spike analysis
spikes1 = [r for r in rows if r['cpu'] > 1.0]
spikes2 = [r for r in rows if r['cpu'] > 2.0]
spikes5 = [r for r in rows if r['cpu'] > 5.0]
print(f"\n--- Spikes ---")
print(f"  >1%: {len(spikes1)} ({100*len(spikes1)/len(rows):.2f}%)")
print(f"  >2%: {len(spikes2)} ({100*len(spikes2)/len(rows):.2f}%)")
print(f"  >5%: {len(spikes5)} ({100*len(spikes5)/len(rows):.2f}%)")

# Top 20 worst
print(f"\n--- Top 20 worst (by CPU%) ---")
worst = sorted(rows, key=lambda r: r['cpu'], reverse=True)[:20]
for i, r in enumerate(worst):
    print(f"  {i+1:2d}. cpu={r['cpu']:.3f}%  dur={r['dur']:.1f}us  bs={r['bs']}  t={r['ts']:.3f}")

# Small blocks inflate CPU% artificially
print(f"\n--- Small blocks (bs < 64) ---")
small = [r for r in rows if r['bs'] < 64]
print(f"  Count: {len(small)}")
if small:
    print(f"  Mean CPU%: {statistics.mean([r['cpu'] for r in small]):.3f}")
    print(f"  Mean dur:  {statistics.mean([r['dur'] for r in small]):.1f}us")

# Normal blocks only
normal = [r for r in rows if r['bs'] >= 128]
ncpus = [r['cpu'] for r in normal]
nsc = sorted(ncpus)
print(f"\n--- Normal blocks (bs>=128) ---")
print(f"  Count: {len(normal)}")
print(f"  Mean CPU%:  {statistics.mean(ncpus):.3f}")
print(f"  Med CPU%:   {statistics.median(ncpus):.3f}")
print(f"  P99 CPU%:   {nsc[int(len(nsc)*0.99)]:.3f}")
print(f"  P99.9 CPU%: {nsc[int(len(nsc)*0.999)]:.3f}")
print(f"  Max CPU%:   {max(ncpus):.3f}")

# Gap analysis (time between blocks > 10ms = possible audio dropout)
gaps = []
for i in range(1, len(rows)):
    gap_ms = (rows[i]['ts'] - rows[i-1]['ts']) * 1000
    if gap_ms > 10:
        gaps.append({'gap_ms': gap_ms, 'idx': i, 'ts': rows[i]['ts'], 'next_bs': rows[i]['bs'], 'next_cpu': rows[i]['cpu']})
print(f"\n--- Large gaps (>10ms between blocks) ---")
print(f"  Count: {len(gaps)}")
if gaps:
    for g in sorted(gaps, key=lambda x: x['gap_ms'], reverse=True)[:10]:
        print(f"  gap={g['gap_ms']:.1f}ms  next_bs={g['next_bs']}  next_cpu={g['next_cpu']:.3f}%")
