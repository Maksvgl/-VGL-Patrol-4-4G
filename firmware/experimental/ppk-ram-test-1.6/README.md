# VGL Patrol 1.6: experimental volatile PPK mode

This extension is for the owner-confirmed 1400 ms / RF 5 ms package only.
It is **not a new hardware-validated production baseline**. The approved
original files remain under `firmware/releases/v1.6-1400ms-rf5ms`.

Read [HOW_TO_TEST_RU.md](HOW_TO_TEST_RU.md) before testing one known-good board.
`dist` contains the Windows delivery files. Only an explicit SWD RAM request
activates factory mode. On ordinary boot, startup clears the entire 64-byte
mailbox and the program uses the activation previously stored in external W25Q.
No new persistence of test activation, secret or device serial is added.

Host fix: reject Java's unrelated `jlink.exe` before all processor operations.
SEGGER identity is checked from PE version information or its exact embedded
Commander banner, with installed SEGGER directories preferred over all PATH
matches. No candidate is executed just to identify it. After the reported
Java collision, use `START_PPK_TEST.bat` to prepare the already flashed image.

Commander compatibility: use explicit hexadecimal SaveBin lengths and do not
treat the probe's unsupported-uptime capability notice as a target-command
failure. Invalid/unsupported operations still fail preparation. Firmware images
remain unchanged by these host-only fixes.

## Reproduction

Install `requirements-build.txt` in an isolated Python environment. From this
experimental directory:

```sh
python build_factory.py --base-dir ../../releases/v1.6-1400ms-rf5ms --output-dir dist
python test_factory.py
python prepare_dist.py --base-dir ../../releases/v1.6-1400ms-rf5ms
```

The builder rejects any baseline whose SHA-256, function signatures or hook
bytes differ. It modifies only two four-byte BL instructions and appends an
assembly extension in previously unused application Flash. It does not rebuild
the unrelated historical C sources or alter the supplied SoftDevice/bootloader.
The source assembly, disassembly and exact patch report are emitted with the
images. Mapping evidence is in `mapping.json`.

## Implementation contract

- Startup hook at `0x292BC`: clear `0x2000FFC0..0x2000FFFF`, stamp startup,
  then call the original reader-start routine.
- Idle hook at `0x292C0`: ordinary path tail-calls the unchanged original idle
  routine; a request requires both 32-bit magic and cookie.
- Factory path consumes its request once, rejects charging/power-saving states,
  validates RTC fields, calls unchanged production RTC write/read routines,
  sets only RAM activation and the original 45875-tick period, and calls the
  unchanged reader-start routine.
- Successful host preparation verifies RTC readback against the PC clock and
  current RAM flags/period, then releases debug power requests.
- Source-level current-delay zero was already fixed in this exact production
  binary; its existing 1.6 helper and dynamic SAADC patches are preserved.

The 64-byte mailbox lies above the mapped stack, heap and existing 32-byte
production extension state. This was checked against the archived IAR ELF/map
and literal references in the exact working image. This static audit is not a
formal proof of all dynamically formed addresses.

The ordinary path has a small additional marker check on each idle iteration.
The normal RFID algorithm and interval remain those of the approved binary.
The original reader routine does not report timer-start failure, so actual
1.4-second RF pulses must be checked on hardware even after a READY result.

`test_factory.py` exercises the emitted Thumb code in Unicorn, including stack
alignment, preserved registers, reset clearing, normal path, success and RTC/
state failure paths. Production peripheral routines are mocked at their verified
entry addresses; these tests do not emulate the complete nRF/SoftDevice/board.

Validation: 15/15 firmware tests and 47/47 host driver mock scenarios passed.
The latter execute the actual PowerShell driver on PowerShell 7.4.13 / Linux,
with fake Nordic/SEGGER executables. They do not establish Windows 5.1, real SWD
or electrical behavior. See `validation_summary.json` and `host_tests/README.md`.

## Reset and restoration

Recover is a separate explicit user action, never an automatic fallback after
a random programming error. It erases nRF user Flash/UICR/RAM, not W25Q/M24/FICR.
Because the supplied FULL HEX omits UICR, the Windows driver checks and, only
when erased, restores `UICR.BOOTLOADERADDR=0x78000`. This value is established
by the actual MBR dispatch and bootloader vectors. Other UICR fields are not
invented or rewritten. The exact original FULL HEX is included for rollback.
