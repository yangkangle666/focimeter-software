# Real-data index

Real files in this directory are versioned software-integration fixtures. A
`real` source label describes where a file came from; it does not mean that the
software output has passed metrology validation.

| Dataset | Contents | Current use |
| --- | --- | --- |
| `multispot_lens_pairs/real_lens_pair_set_001/` | One shared no-lens reference and two lens measurements | Completed phase-one M1 -> M2 -> M3 software integration baseline |
| `multispot_lens_pairs/real_lens_repeat_set_002/` | Three confirmed lenses, one no-lens reference and four stationary repeats per lens | Phase-two first run completed; M1 12/12, M2 0/12 due candidate noise, M3 not run |

Read each dataset's `README.md` and `manifest.json` before use. Keep algorithm
outputs under `focimeter_system/outputs/`; do not commit generated results as
raw input data. New data must have confirmed pairing, stable names, exact
SHA-256 hashes, acquisition notes, explicit exclusions, and a clear validation
boundary before it is added here.
