# Milestone 3 Final Report

All final rows were run with `--verify` and `-r 3`. The final winner summary is
tracked in `final_winners.csv`. Raw benchmark CSVs are generated under
`results/` by `scripts/run_milestone3_mix.sh`; that directory is intentionally
ignored because repeated benchmark runs can overwrite it. Plots for throughput
and index size are in this directory.

## Final Winners

| Dataset | Workload | Winning configuration | Avg throughput (Mops/s) | Index size (bytes) | LIPP avg (Mops/s) | Beats LIPP |
| --- | --- | --- | ---: | ---: | ---: | :---: |
| books | 10% insert / 90% lookup | `BinarySearch-e32-s2147483648-f134217728-bf` | 5.913900 | 11,824,266,848 | 5.162543 | yes |
| books | 90% insert / 10% lookup | `BinarySearch-e256-s2147483648-f134217728` | 4.550833 | 11,688,164,380 | 3.977630 | yes |
| fb | 10% insert / 90% lookup | `BinarySearch-e32-s2147483648-f134217728-bf` | 3.625223 | 12,704,781,248 | 3.264503 | yes |
| fb | 90% insert / 10% lookup | `BinarySearch-e256-s2147483648-f134217728` | 3.912443 | 12,540,345,108 | 3.230547 | yes |
| osmc | 10% insert / 90% lookup | `BinarySearch-e32-s2147483648-f134217728-bf` | 4.317533 | 20,615,828,032 | 2.764082 | yes |
| osmc | 90% insert / 10% lookup | `BinarySearch-e256-s2147483648-f134217728` | 3.986553 | 20,288,379,268 | 2.244187 | yes |

## Design Summary

The final implementation uses a split policy. Lookup-heavy workloads use the
large deferred overlay with bloom-style overlay miss filtering and the `e16/e32`
candidate sweep; `e32` won all three lookup-heavy datasets. Insert-heavy
workloads use the large deferred DPGM path with the `e256` candidate. A cheap
one-hash membership filter remains on that path to avoid negative DPGM probes;
payload data is still stored only in LIPP/DPGM structures.

The central result is unchanged: bloom-guided overlay miss filtering is the
right optimization for lookup-heavy mixed workloads, while the large deferred
overlay path is best for insert-heavy mixed workloads.

## Generated Plots

- `milestone3_books_10pct_insert_throughput.png`
- `milestone3_books_10pct_insert_index_size.png`
- `milestone3_books_90pct_insert_throughput.png`
- `milestone3_books_90pct_insert_index_size.png`
- `milestone3_fb_10pct_insert_throughput.png`
- `milestone3_fb_10pct_insert_index_size.png`
- `milestone3_fb_90pct_insert_throughput.png`
- `milestone3_fb_90pct_insert_index_size.png`
- `milestone3_osmc_10pct_insert_throughput.png`
- `milestone3_osmc_10pct_insert_index_size.png`
- `milestone3_osmc_90pct_insert_throughput.png`
- `milestone3_osmc_90pct_insert_index_size.png`
