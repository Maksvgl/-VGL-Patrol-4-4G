# Host driver mock tests

`run_tests.py` executes the actual `../windows/PPK_Test.ps1` in PowerShell, with executable Python fakes for `nrfjprog.exe` and `JLink.exe`. No real programmer or board is accessed. All fixture directories, firmware paths, and working directories contain spaces to exercise native argument handling.

Run from any directory with Python 3.9+ and a PowerShell executable:

```sh
python3 run_tests.py --pwsh /absolute/path/to/pwsh
```

The default executable is `runtime/pwsh`. The local review used Microsoft PowerShell **7.4.13** from the official pinned release:

https://github.com/PowerShell/PowerShell/releases/download/v7.4.13/powershell-7.4.13-linux-x64.tar.gz

`results.json` records the driver SHA-256 and each result. Ignored `cases/` directories retain full console output, tool traces, simulated memory, and the driver's per-run logs.

Current run: **47/47 passed**, including seven Java/SEGGER discovery regressions
and six real-output classification regressions.

Coverage includes package validation before tool access; ordinary and recover flashing; UICR checks/restoration and ordering; RAM-only start; normal-boot checks with inactive/active external settings; approved-firmware restoration and its post-boot verification; explicit probe selection; multiple probes; checksum and manifest path rejection; protected/unexpected configuration; absent startup stamp; disabled IRQ20; pending request; dirty startup mailbox; RTC failures; invalid/stale RTC readback; wrong cookies/ticks/activation/coil/power-saving flags; parsed J-Link error output with exit code zero; illegal option combinations; and final debug power-release ordering.

The J-Link discovery regression cases reproduce Java `jlink.exe` appearing first
on PATH. They cover a real SEGGER identity in a Program Files installation, a
later SEGGER PATH entry, Java alone, no J-Link, and explicit Java/SEGGER/missing
paths. Missing or incorrect SEGGER must be rejected before even listing Nordic
probes, and therefore before recovery or programming. Discovery inspects the
executable without running it: the genuine mock contains the exact embedded
Commander banner, while the separate Java mock does not. Windows PE version
metadata itself is not exercised by these Linux fixtures.

The SaveBin fake follows J-Link's hexadecimal address/count semantics, including
bare digits. The harness asserts mailbox dumps are 64 bytes, flag dumps 8 bytes,
register dumps 4 bytes, and that mailbox reads stay inside nRF52832 RAM. Against
the old distributed driver, `SaveBin ..., 64` produced 100 bytes and reproduced
`mailbox_before.bin has an unexpected length`. The exact real output line
`J-Link uptime (since boot): N/A (Not supported by this model)` also reproduced
the old driver's false `J-Link reported a failure` result.

The corrected driver succeeds when that benign uptime line appears in all three
J-Link sessions, including the final debug power release. Zero-exit genuine
errors are still rejected: Unknown command, both command-not-supported forms,
Syntax, Cannot connect to target, and explicit ERROR. These observations are
recorded under `regression_evidence` in `results.json`.

The fake J-Link consumes a factory request only after the driver writes the IRQ20 pending bit. Successful flows must finish with `Go`, `WriteDP 2, 0`, `WriteDP 1, 0`, `Exit`, with no later tool access. Non-flash flows must not reset/recover/write UICR/program flash. Normal checks and approved restoration must not send a RAM factory request. Failed operations must not produce a success result file.

This is a host control-flow test on **PowerShell 7 / Linux**. It does not establish Windows PowerShell 5.1 compatibility, real SEGGER/Nordic CLI behavior, correct SWD register access, successful firmware execution, RTC electrical behavior, or measured PPK consumption. Real Windows and board verification remain separate.
