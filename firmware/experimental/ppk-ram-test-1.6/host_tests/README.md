# Host driver mock tests

`run_tests.py` executes the actual `../windows/PPK_Test.ps1` in PowerShell, with executable Python fakes for `nrfjprog.exe` and `JLink.exe`. No real programmer or board is accessed. All fixture directories, firmware paths, and working directories contain spaces to exercise native argument handling.

Run from any directory with Python 3.9+ and a PowerShell executable:

```sh
python3 run_tests.py --pwsh /absolute/path/to/pwsh
```

The default executable is `runtime/pwsh`. The local review used Microsoft PowerShell **7.4.13** from the official pinned release:

https://github.com/PowerShell/PowerShell/releases/download/v7.4.13/powershell-7.4.13-linux-x64.tar.gz

`results.json` records the driver SHA-256 and each result. Ignored `cases/` directories retain full console output, tool traces, simulated memory, and the driver's per-run logs.

Coverage includes package validation before tool access; ordinary and recover flashing; UICR checks/restoration and ordering; RAM-only start; normal-boot checks with inactive/active external settings; approved-firmware restoration and its post-boot verification; explicit probe selection; multiple probes; checksum and manifest path rejection; protected/unexpected configuration; absent startup stamp; disabled IRQ20; pending request; dirty startup mailbox; RTC failures; invalid/stale RTC readback; wrong cookies/ticks/activation/coil/power-saving flags; parsed J-Link error output with exit code zero; illegal option combinations; and final debug power-release ordering.

The fake J-Link consumes a factory request only after the driver writes the IRQ20 pending bit. Successful flows must finish with `Go`, `WriteDP 2, 0`, `WriteDP 1, 0`, `Exit`, with no later tool access. Non-flash flows must not reset/recover/write UICR/program flash. Normal checks and approved restoration must not send a RAM factory request. Failed operations must not produce a success result file.

This is a host control-flow test on **PowerShell 7 / Linux**. It does not establish Windows PowerShell 5.1 compatibility, real SEGGER/Nordic CLI behavior, correct SWD register access, successful firmware execution, RTC electrical behavior, or measured PPK consumption. Real Windows and board verification remain separate.
