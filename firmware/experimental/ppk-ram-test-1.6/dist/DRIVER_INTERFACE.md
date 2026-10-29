# Windows driver interface

Flatten these files into the deliverable directory beside the HEX and
`flash_manifest.json`. The driver supports Windows PowerShell 5.1, Nordic
`nrfjprog.exe`, and SEGGER J-Link Commander 7.92d or later.

J-Link discovery verifies SEGGER identity before any nrfjprog/target operation.
It checks installed SEGGER directories first, then every PATH match, rejecting
Java's unrelated `jlink.exe`. Explicit `-JLinkPath` also requires SEGGER identity.
Identity is read from the PE version resource or exact embedded Commander banner;
candidates are not launched for detection. The selected full path is printed.

SaveBin lengths are explicit hexadecimal (`0x40`, `0x08`, `0x04`), as required
by Commander. A probe uptime capability notice is informational; unknown,
unsupported, malformed or failed target operations remain fatal.

Expected manifest, all filenames relative to the deliverable directory:

```json
{
  "firmware_file": "VGL_Patrol_1.6_PPK_RAM_TEST_FULL_S132_BOOTLOADER.hex",
  "files": [
    {"file": "VGL_Patrol_1.6_PPK_RAM_TEST_FULL_S132_BOOTLOADER.hex", "sha256": "64 hexadecimal characters"}
  ]
}
```

Include every shipped executable/script/image in the checksum list, excluding
the manifest itself. This interface does not require that documentation be
listed, but listed documentation is checked as well.
Include `VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex` in this list
to enable the restore commands. `firmware_file` remains the candidate filename.

- `FLASH_AND_START_PPK_TEST.bat [probe-serial]`: checks UICR before programming;
  never invokes recover automatically.
- `RECOVER_AND_START_PPK_TEST.bat [probe-serial]`: explicit processor erase,
  then the same programming/preparation path.
- `START_PPK_TEST.bat [probe-serial]`: verifies the installed image and prepares
  RAM; it does not flash, reset, or write UICR.
- `CHECK_NORMAL_BOOT.bat [probe-serial]`: after a manual board power cycle,
  verifies the candidate image, the startup stamp, and all 60 cleared payload
  bytes (including request/status/acknowledgement). It never resets/unlocks the board, changes RTC, or sends a
  factory request. Existing normal activation `0` or `1` is accepted. A protected
  SWD read makes the check inconclusive; do not recover just to satisfy it.
- `RESTORE_APPROVED_1_6.bat [probe-serial]`: programs/verifies the unchanged
  approved HEX and restores the known bootloader UICR address when erased;
  then resets, verifies the approved HEX again after the bootloader has run,
  and releases debug power without sending a factory request.
- `RECOVER_AND_RESTORE_APPROVED_1_6.bat [probe-serial]`: the same restoration
  after an explicit processor Flash/UICR/RAM erase.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\PPK_Test.ps1
  -ValidateOnly`: local package checks only, no hardware access.
- Advanced PowerShell parameters: `-ProbeSerial`, `-JLinkPath`, `-Flash`,
  `-Recover`, `-CheckNormal`, `-RestoreApproved`. Both `-Recover` and
  `-RestoreApproved` require `-Flash`. `-CheckNormal` cannot be combined with
  flashing/restoration. `-ValidateOnly` excludes all hardware-operation switches.

The single-probe check fails when there is no probe or more than one without
an explicit serial. The same selected serial is passed to every tool operation.

UICR flow: read `0x10001014` before non-recover programming; accept only erased
`FFFFFFFF` or known `00078000`. Program the full HEX with `--sectorerase
--verify`. If erased, restore only `BOOTLOADERADDR=0x78000` and read it back.
No erase of another UICR word is performed. An unrecognized configuration or
locked read fails with guidance to use the explicit recover workflow.

All J-Link command files use ASCII relative output filenames in a unique run
directory. Local register/RTC dumps contain no secret. The successful final
target commands are `Halt`, `Go`, `WriteDP 2, 0`, `WriteDP 1, 0`, `Exit`; all later
operations are host-only reporting. Do not add a target read after that phase.
The brief Halt makes Go valid even when an earlier session left the core running;
it is not a reset and does not clear the volatile test state or RTC.

The driver checks the 64-byte mailbox, status/cookie, read/write RTC return
codes, RTC date, active/coil/power-saving RAM flags, and 45875-tick interval.
Readback success is preparation evidence, not a substitute for observing
actual RFID pulses and current with PPK2.

## Waking an inactive board

At normal startup the existing application enables its app_timer SWI0 interrupt,
IRQ20. Before preparing a test the driver reads NVIC ISER0 at `0xE000E100` and
requires bit `0x00100000` to be enabled; it never enables an interrupt itself.
After committing the RAM request magic and before `Go`, it writes only this
bit to NVIC ISPR0 (`0xE000E200`). This pends the already configured app_timer
SWI0 once so an inactive board waiting in the SoftDevice event wait can wake
and process the mailbox. No other NVIC register is written. The handler and
its empty-queue path were mapped in the exact supplied 1.6 application.
`-CheckNormal` only reads ISER0 and never pends the interrupt.
