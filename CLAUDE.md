# VibeGuard: notes for Claude

Read README.md first, then docs/BRIEF.md for background. The docs are the source of truth. Keep them current when the code changes.

## Rules

- `firmware/VibeGuard/vg_*.h` must stay free of Arduino dependencies: the host tests compile them with clang++. Hardware code belongs in `VibeGuard.ino` only.
- All tunables live in `vg_config.h`. Don't scatter magic numbers.
- `wokwi/sketch.ino` is generated. Edit the firmware, then run `python3 tools/build_wokwi.py`.
- After changing the engine: run `./tests/run_tests.sh` and update the numbers in `docs/TEST_REPORT.md`, which come from `tests/results/summary.md`.
- The dashboard HTML lives only in `vg_dashboard.h`. The mock server extracts it with a regex on the `R"VGHTML(` delimiter.

## Environment facts (this Mac)

- Available: clang++, python3 (no numpy or matplotlib), node, brew. Not installed: arduino-cli, PlatformIO, wokwi-cli.
- Wokwi web: load the files with `monaco.editor.getModels()` in the page. The page can't fetch from localhost (blocked), so gzip and base64 the sketch and decompress it in the page with `DecompressionStream`. Keep the browser pane visible: when hidden, the simulation is throttled to about 3%. When visible, it runs at 100% real time. Send serial commands by typing into the serial monitor textbox (find "textbox"; its ref changes after each restart).
