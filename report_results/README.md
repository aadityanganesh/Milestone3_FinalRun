# Report results

This folder holds the tracked Milestone 3 result artifacts that are safe to
push to GitHub.

## Contents

| Path | Description |
|------|-------------|
| `milestone3_final/` | Final report, final-winner CSV, and the final bar charts |
| `milestone3_good_run/` | Earlier good-run bar charts and summary tables |
| `plot_milestone3_good_run.py` | Regenerates the good-run plots |
| `../scripts/plot_milestone3_mix.py` | Regenerates the final plots from benchmark CSVs in `results/` |

## Regenerate Final Plots

From the repository root, after running `scripts/run_milestone3_mix.sh`:

```bash
python scripts/plot_milestone3_mix.py \
  --results-dir results \
  --out-dir report_results/milestone3_final
```

## Chart Naming

- `10pct_insert_*` means 10% insert / 90% lookup.
- `90pct_insert_*` means 90% insert / 10% lookup.
- `*_throughput.png` contains throughput in M operations/s.
- `*_index_size.png` contains index size in GB.
