# Identity reset Windows driver

Primary command: `RESET_IDENTITY_AND_RESTORE_1_6.bat [probe-serial]`.
It explicitly recovers the nRF, loads the reset service, requests the persistent
23-byte identity reset through SWD, checks fresh external readback, then installs
and verifies the exact ordinary approved 1.6 FULL HEX.

The final ordinary RAM check requires inactive=0 and original ASCII-zero secret
and serial defaults. Only after that check does the driver finish with
`Halt`, `Go`, `WriteDP 2, 0`, `WriteDP 1, 0`, `Exit`; no target access follows.
No test activation, test secret, RTC setting or event/counter erase is requested.

Requirements: Windows PowerShell 5.1+, Nordic nrfjprog on PATH, SEGGER J-Link
Commander 7.92d+ and one selected SWD probe. SEGGER identity is checked before
processor access, rejecting Java jlink.exe. Explicit PowerShell `-JLinkPath`
and `-ProbeSerial` are supported. `-ValidateOnly` checks package bytes only.

Manifest: `flash_manifest.json` must have `firmware_file` equal to the reset
service FULL HEX filename and a `files` array of flat filenames with SHA-256.
Both service and ordinary image must be listed. The ordinary HEX also has a
literal known SHA-256 guard in the driver.

Mailbox: 0x2000FFC0, magic31525346, cookieA6F01602, stamp31525342 at+60.
Header status+8 must be2, write/read returns+12/+16 must be0, verified length+20
must be23, acknowledgement+24 must equal cookie. Only the32-byte header and
separate stamp are dumped until success, so failed resets do not dump old secret
scratch. On success only, new23-byte readback at+32 must equal00 followed by22
ASCII zero characters. SaveBin lengths are explicit hexadecimal.

Service requests pend only already-enabled IRQ20 after committing cookie/magic.
No disabled interrupt is enabled. Busy status is polled with a bounded wait.
Any unknown/error result stops before ordinary restoration. A failed run may
leave the service image installed; rerun the complete explicit reset workflow
after resolving its cause. Host logs never intentionally contain the old secret.

The reset scope is activation, secret and serial. Existing events/counters and
other sector contents are preserved by the verified production Flash writer's
read/modify/write operation under uninterrupted power. The kit is not an atomic
transaction against power loss.
