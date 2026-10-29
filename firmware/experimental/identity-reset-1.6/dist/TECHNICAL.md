# VGL Patrol 1.6: persistent identity reset and approved firmware restoration

This is a dedicated service workflow, separate from the volatile PPK test kit.
It permanently restores activation, secret and serial to the exact application's
unconfigured values: one zero byte and 22 ASCII `0` bytes at W25Q offsets 4..26.
Other external data is intentionally retained. No test activation is performed.

The primary Windows BAT explicitly recovers the nRF, installs a hash-locked
service extension, submits a one-shot SWD request, checks fresh persistent
readback, installs the unchanged approved FULL 1.6 image, verifies it after
boot and checks that the ordinary application's loaded identity is unconfigured.
The final image contains S132 and the original bootloader, without the service
extension. Existing production baseline files are not replaced in Git.

Validation: 19/19 emitted-firmware tests and 48/48 host mock scenarios passed.
Independent emitted-code/ABI review completed; no board test performed.

See [README_RU.md](README_RU.md) for the short operator procedure and material
scope. Hardware validation of this new reset workflow remains outstanding.

## Reproduction

Install `requirements-build.txt` in an isolated Python environment. From this
experimental directory, the baseline is `../../releases/v1.6-1400ms-rf5ms`:

```sh
python build_reset.py --base-dir ../../releases/v1.6-1400ms-rf5ms --output-dir dist
python test_reset.py
python prepare_dist.py --base-dir ../../releases/v1.6-1400ms-rf5ms
```

The builder checks the approved BIN/FULL SHA-256, verified function signatures,
two original BL instructions and available Flash/RAM ranges. This is an
assembly extension of that exact binary, not a rebuild of the historical C tree.
Mapping evidence, assembly and disassembly are included with the build.

## Persistence and failure handling

`settingsCommit()` alone is insufficient: its production implementation ignores
individual write failures. The service uses the existing checked W25Q sector
read/modify/write routine, then independently reads and compares all 23 bytes.
Acknowledgement is issued only after exact content verification. Failed/partial
readbacks are not exported as identity dumps. A failed write never proceeds
to the success/ordinary-restoration path.

The reset intentionally does not invoke first-boot initialization or erase an
entire external chip. Those operations would also clear event/counter data.
The service quiesces RFID/shock callbacks and protects its external-memory
operation against competing application callbacks. It preserves higher-priority
SoftDevice interrupts and restores the previous interrupt-mask state on exit.
The ordinary restoration must erase the service helper in the last touched
application page as well as restoring the original call instructions.

Host tests run the actual PowerShell script with simulated Nordic/J-Link tools;
firmware tests execute emitted Thumb code with production peripheral calls
mocked at verified entry addresses. Neither substitutes for the first board test.
