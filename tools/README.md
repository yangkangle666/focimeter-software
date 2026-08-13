# Repository tools

`meeting_demo_phase1.ps1` runs the previously verified phase-one M1 -> M2 ->
M3 demonstration against `real_lens_pair_set_001`. It does not run the new
phase-two repeated-capture dataset.

The script expects the meeting M2 binary at
`/root/focimeter-m2-meeting-build/focimeter_m2` inside WSL Ubuntu. Runtime
results are written below `focimeter_system/outputs/` and are ignored by Git.
