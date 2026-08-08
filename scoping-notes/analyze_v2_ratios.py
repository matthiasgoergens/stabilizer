# /// script
# requires-python = ">=3.11"
# dependencies = ["pandas", "numpy", "scipy"]
# ///
"""Within-pair ratio analysis of the load-robust v2 batch. Primary
estimand = treatment_wall / paired_PLAIN_wall (load cancels in the pair).
Drops rc=127 rows (PADDED seeds 16-20 never built) and any pair missing a
role. Reports per arm/config: ratio mean, CV, seed-level BCa bootstrap CI;
PADDED between-seed variance component (method of moments) on ratios;
normality (the brief's prediction) on the right, aggregation-matched
distributions."""
import numpy as np, pandas as pd
from scipy import stats

df = pd.read_csv("/home/matthias/prog/stabilizer-baseline/results_v2.csv")
df = df[df["correct"].astype(str).isin(["1", "True", "true"])].copy()
df["wall_time_s"] = df["wall_time_s"].astype(float)

def pairs(sub):
    out = []
    for pid, g in sub.groupby("pair_id"):
        t = g[g["role"] == "treatment"]; p = g[g["role"] == "plain_ref"]
        if len(t) == 1 and len(p) == 1 and float(p["wall_time_s"].iloc[0]) > 0:
            r = float(t["wall_time_s"].iloc[0]) / float(p["wall_time_s"].iloc[0])
            out.append({"benchmark": t["benchmark"].iloc[0], "arm": t["arm"].iloc[0],
                        "config": str(t["config"].iloc[0]), "variant": str(t["variant"].iloc[0]),
                        "ratio": r, "run_order": int(t["run_order"].iloc[0]),
                        "load1": float(t["load1"].iloc[0])})
    return pd.DataFrame(out)

def boot_ci(vals, nboot=10000):
    vals = np.asarray(vals)
    if len(vals) < 3: return (np.nan, np.nan)
    idx = np.random.default_rng(20260808).integers(0, len(vals), (nboot, len(vals)))
    return tuple(np.percentile(vals[idx].mean(axis=1), [2.5, 97.5]))

P = pairs(df)
print(f"valid pairs: {len(P)} (of 496; dropped orphans/rc127)")
print(f"PLAIN absolute-time CV for context (load swing): "
      f"{100*df[df.role=='plain_ref']['wall_time_s'].std(ddof=1)/df[df.role=='plain_ref']['wall_time_s'].mean():.1f}%")

for bench in sorted(P["benchmark"].unique()):
    b = P[P["benchmark"] == bench]
    print(f"\n{'='*66}\n{bench}\n{'='*66}")
    print("-- within-pair ratio by arm/config (load-cancelled) --")
    for (arm, cfg), g in b.groupby(["arm", "config"]):
        r = g["ratio"]; lo, hi = boot_ci(r.values)
        print(f"  {arm:11s}/{cfg:6s} n={len(r):3d} ratio={r.mean():.4f} "
              f"CV={100*r.std(ddof=1)/r.mean():5.2f}% 95%CI=[{lo:.4f},{hi:.4f}] "
              f"overhead={100*(r.mean()-1):+.1f}%")
    # PADDED between-seed variance component on ratios (MoM)
    pad = b[b["arm"] == "PADDED"]
    if len(pad):
        groups = [g["ratio"].to_numpy() for _, g in pad.groupby("variant")]
        k = len(groups); ns = np.array([len(g) for g in groups]); N = ns.sum()
        grand = pad["ratio"].mean()
        ssb = sum(len(g)*(g.mean()-grand)**2 for g in groups)
        ssw = sum(((g-g.mean())**2).sum() for g in groups)
        msb, msw = ssb/(k-1), ssw/(N-k); n0 = (N-(ns**2).sum()/N)/(k-1)
        s2b = max(0.0, (msb-msw)/n0); frac = s2b/(s2b+msw) if (s2b+msw)>0 else 0
        F = msb/msw; pval = stats.f.sf(F, k-1, N-k)
        # bootstrap CI on sigma_b (% of mean) at seed level
        rng = np.random.default_rng(20260808); sims=[]
        for _ in range(4000):
            gs = [groups[i] for i in rng.integers(0,k,k)]
            gg = np.concatenate(gs); m2=len(gs)
            nsx=np.array([len(x) for x in gs]); Nx=nsx.sum()
            gr=gg.mean(); sb=sum(len(x)*(x.mean()-gr)**2 for x in gs)/(m2-1)
            sw=sum(((x-x.mean())**2).sum() for x in gs)/(Nx-m2)
            n0x=(Nx-(nsx**2).sum()/Nx)/(m2-1)
            sims.append(np.sqrt(max(0.0,(sb-sw)/n0x))/gr*100)
        lo,hi=np.percentile(sims,[2.5,97.5])
        print(f"  PADDED between-seed: {100*frac:.1f}% of ratio var; "
              f"sigma_b={100*np.sqrt(s2b)/grand:.3f}% of mean "
              f"(95% CI [{lo:.3f}%,{hi:.3f}%]); ANOVA F={F:.2f} p={pval:.4f}")
        print(f"  GATE 0.5% line: sigma_b upper-95% = {hi:.3f}% -> "
              f"{'BELOW (cheap route controls it here)' if hi<0.5 else 'ABOVE/STRADDLES (indeterminate or insufficient)'}")
        # normality (brief's prediction), aggregation-matched
        seedmeans = pad.groupby("variant")["ratio"].mean()
        sm = stats.shapiro(seedmeans)
        print(f"  normality PADDED per-seed mean ratios (n={len(seedmeans)}): "
              f"W={sm.statistic:.3f} p={sm.pvalue:.4f} "
              f"({'non-normal' if sm.pvalue<0.05 else 'not distinguishable from normal'})")
    for cfg in ["all","code","stack","heap"]:
        s = b[(b["arm"]=="STABILIZER")&(b["config"]==cfg)]["ratio"]
        if len(s)>=3:
            ss=stats.shapiro(s)
            print(f"  normality STABILIZER/{cfg:5s} ratios (n={len(s)}): "
                  f"W={ss.statistic:.3f} p={ss.pvalue:.4f} "
                  f"({'non-normal' if ss.pvalue<0.05 else 'not distinguishable from normal'})")
    # run-order sanity on ratios (should be ~0 if pairing worked)
    r,pv = stats.pearsonr(b["run_order"], b["ratio"])
    print(f"  ratio vs run_order: r={r:+.3f} p={pv:.4f} (want ~0: pairing removed drift)")
