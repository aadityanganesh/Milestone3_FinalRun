# Milestone 3 Final Report

Run protocol:

- Verification enabled: yes (`--verify`)
- Repeats: 3 per benchmark invocation
- Workload size: 2M mixed operations
- Base size: 100M-key public traces
- Results directory: `results/`
- Plot directory: `report_results/milestone3_final/`

## Final Winners

Each baseline column is the best measured row for that family in the final CSV, averaged across the same 3 verified repeats.

| Dataset | Workload | Winning HybridPGMLippAdv config | HybridPGMLippAdv Mops/s | LIPP Mops/s | DynamicPGM Mops/s | Naive HybridPGMLipp Mops/s | Beats all baselines | Index size bytes |
| --- | --- | --- | ---: | ---: | ---: | ---: | :---: | ---: |
| books | 10% insert / 90% lookup | `BinarySearch-e32-s2147483648-f134217728-bf` | 6.188153 | 5.131503 | 1.043450 | 1.162667 | yes | 11,927,060,512 |
| books | 90% insert / 10% lookup | `BinarySearch-e256-s2147483648-f134217728` | 5.216470 | 3.549817 | 3.169480 | 3.515557 | yes | 11,688,164,380 |
| fb | 10% insert / 90% lookup | `BinarySearch-e32-s2147483648-f134217728-bf` | 6.109020 | 4.787563 | 0.957548 | 1.123760 | yes | 12,719,868,968 |
| fb | 90% insert / 10% lookup | `BinarySearch-e256-s2147483648-f134217728` | 5.276000 | 2.782507 | 3.154510 | 3.399323 | yes | 12,540,345,108 |
| osmc | 10% insert / 90% lookup | `BinarySearch-e32-s2147483648-f134217728-bf` | 5.014877 | 3.854043 | 1.053757 | 1.209500 | yes | 20,620,384,496 |
| osmc | 90% insert / 10% lookup | `BinarySearch-e256-s2147483648-f134217728` | 4.072857 | 2.617643 | 3.118210 | 3.209390 | yes | 20,288,379,268 |

## Submission Readiness

- The final CSVs include `DynamicPGM`, `LIPP`, `HybridPGMLipp`, and `HybridPGMLippAdv`.
- The final plots compare all four index families.
- `HybridPGMLippAdv` beats the best measured `DynamicPGM`, `LIPP`, and naive `HybridPGMLipp` row on all six benchmark tasks.
- The benchmark binary was rebuilt successfully after the final source changes.

## Implementation Summary

- Lookup-heavy lane uses LIPP for stored keys plus range/bloom-style side filters to reject definite misses before expensive lookups.
- Insert-heavy lane keeps the deferred e256 path with the large local threshold.
- Final benchmark dispatch remains split by workload family:
  - `0.100000i_0m_mix`: e16/e32 lookup-heavy candidates
  - `0.900000i_0m_mix`: e256 insert-heavy candidate

## Generated Artifacts

Throughput and index-size plots were regenerated for all six cases in:

`report_results/milestone3_final/`
