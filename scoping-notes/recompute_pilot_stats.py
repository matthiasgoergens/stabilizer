# /// script
# requires-python = ">=3.11"
# dependencies = ["pandas", "scipy", "numpy"]
# ///
"""Independent recomputation of the pilot's headline statistics,
prompted by a DeepSeek refutation of the power sketch. Reads the raw
results.csv; prints per-arm summary, one-way ANOVA variance components
for PADDED, a time-trend check, and corrected power arithmetic."""

import numpy as np
import pandas as pd
from scipy import stats

df = pd.read_csv("/home/matthias/prog/stabilizer-baseline/results.csv")
assert (df["correct"] == 1).all(), "incorrect runs present"

print("== per-arm summary ==")
for arm, g in df.groupby("arm"):
    w = g["wall_time_s"]
    sw_p = stats.shapiro(w).pvalue if len(w) >= 3 else float("nan")
    print(
        f"{arm:8s} n={len(w):3d} mean={w.mean():.4f}s sd={w.std(ddof=1):.4f}s "
        f"CV={100 * w.std(ddof=1) / w.mean():.3f}% shapiro_p={sw_p:.4f}"
    )

print("\n== PADDED one-way ANOVA variance components (method of moments) ==")
p = df[df["arm"] == "PADDED"]
groups = [g["wall_time_s"].to_numpy() for _, g in p.groupby("variant")]
k = len(groups)
ns = np.array([len(g) for g in groups])
n0 = (ns.sum() - (ns**2).sum() / ns.sum()) / (k - 1)  # avg group size, unbalanced-safe
grand = p["wall_time_s"].mean()
ssb = sum(len(g) * (g.mean() - grand) ** 2 for g in groups)
ssw = sum(((g - g.mean()) ** 2).sum() for g in groups)
msb = ssb / (k - 1)
msw = ssw / (ns.sum() - k)
sigma2_b = max(0.0, (msb - msw) / n0)
sigma2_w = msw
frac_between = sigma2_b / (sigma2_b + sigma2_w)
print(f"k={k} groups, n per group={ns.tolist()}")
print(f"MSB={msb:.6f} MSW={msw:.6f}")
print(f"sigma_b={np.sqrt(sigma2_b):.4f}s sigma_w={np.sqrt(sigma2_w):.4f}s")
print(f"between-seed fraction of variance = {100 * frac_between:.2f}%")
print(f"between-seed sd as % of mean (the layout-lottery bias scale) = "
      f"{100 * np.sqrt(sigma2_b) / grand:.3f}%")
f_stat = msb / msw
p_anova = stats.f.sf(f_stat, k - 1, ns.sum() - k)
print(f"ANOVA F={f_stat:.3f} p={p_anova:.5f}")

print("\n== time-trend check (does run order explain variance?) ==")
for arm, g in df.groupby("arm"):
    g = g.sort_values("epoch_unix")
    r, pv = stats.pearsonr(np.arange(len(g)), g["wall_time_s"])
    print(f"{arm:8s} order-vs-time r={r:+.3f} p={pv:.4f}")

print("\n== power arithmetic, three ways (detect 1% effect, 80% power, alpha=.05, two-sided) ==")
single_sd = df[df["arm"] == "SINGLE"]["wall_time_s"].std(ddof=1)
single_mean = df[df["arm"] == "SINGLE"]["wall_time_s"].mean()
delta = 0.01 * single_mean
for label, sd in [
    ("SINGLE run-to-run sd (one fixed binary per arm)", single_sd),
    ("PADDED within-seed sd", np.sqrt(sigma2_w)),
    ("PADDED total sd (between+within)", p["wall_time_s"].std(ddof=1)),
]:
    n = 2 * ((1.96 + 0.8416) ** 2) * (sd / delta) ** 2
    print(f"{label:50s} sd={sd:.4f}s -> n/group ~= {n:.1f}")
print(f"\nirreducible single-build bias scale (sigma_b as % of mean): "
      f"{100 * np.sqrt(sigma2_b) / grand:.3f}% -- no n fixes this in a "
      f"one-binary-per-arm design; it is a bias, not noise")
