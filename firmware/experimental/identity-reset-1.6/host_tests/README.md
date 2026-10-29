# Reset host driver mock tests

`run_tests.py` executes the actual `../windows/Reset_Identity.ps1` in official
PowerShell 7.4.13/Linux with fake Python `nrfjprog.exe` and `JLink.exe`.
Validation: **48/48 scenarios passed**. No real probe, board or external Flash is accessed. The unchanged approved FULL
HEX from the repository is copied into fixtures to exercise the driver's literal
approved-image SHA-256 check. Fixture paths contain spaces.

Run:

```sh
python3 run_tests.py --pwsh /absolute/path/to/pwsh
```

The default runtime is the previously downloaded official PowerShell under
`../../patrol_factory_ppk/host_tests/runtime/pwsh`. Runtime and case folders are
not deliverable dependencies. Results record the exact driver SHA-256.

Coverage: explicit recovery; service/ordinary program and post-boot verification
ordering; UICR restoration; IRQ20-gated request and bounded busy polling;
write/read/mismatch/state failures; request acknowledgement and full23-byte
persistent readback; ordinary inactive/defaults checks; failures do not restore
ordinary prematurely or report success; no scratch readback dumps on failed
clear; original ordinary-image SHA guard; Java/SEGGER discovery before hardware;
probe selection; strict hexadecimal SaveBin lengths; persistent CPU running/
halted state; benign uptime notice; real unknown/unsupported/syntax/connection
errors; final Halt/Go/DP-release order without later target access.

These tests establish host control flow only. They do not prove Windows5.1,
real Nordic/SEGGER command behavior, service firmware execution, EEPROM/SPI
electrical behavior, RF consumption, or power-loss atomicity. Those require a
known-good physical board and retained hardware logs.
