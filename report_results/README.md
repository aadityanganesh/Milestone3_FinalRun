# Report results

This folder holds the tracked Milestone 3 result artifacts that are safe to
push to GitHub.

## Contents

| Path | Description |
|------|-------------|
| `milestone3_final/` | Final report, final-winner CSV, and the 12 final bar charts |
| `milestone3_good_run/` | 12 PNG bar charts + `tables_good_run.md` (12 markdown tables, one per figure) |
| `plot_milestone3_good_run.py` | Regenerates those files (requires `matplotlib`) |
| `../scripts/plot_milestone3_mix.py` | Regenerates the final plots from benchmark CSVs in `results/` |

## Regenerate final plots

From the repository root, after running `scripts/run_milestone3_mix.sh`:

```bash
python scripts/plot_milestone3_mix.py \
  --results-dir results \
  --out-dir report_results/milestone3_final
```

## Regenerate good-run plots

From the **repository root**:

```bash
pip install matplotlib
python report_results/plot_milestone3_good_run.py
```

Outputs are written to `report_results/milestone3_good_run/`.

## Chart naming

- `10pct_insert_*` — 10% insert / 90% lookup (lookup-heavy)
- `90pct_insert_*` — 90% insert / 10% lookup (insert-heavy)
- `*_throughput.png` — LIPP vs HybridPGMLippAdv vs DynamicPGM (M ops/s)
- `*_index_size.png` — same three, index size in GB
