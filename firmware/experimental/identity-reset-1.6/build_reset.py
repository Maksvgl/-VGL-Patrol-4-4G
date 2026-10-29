#!/usr/bin/env python3
"""Build a one-shot identity-reset service extension for ONE hash-locked VGL Patrol 1.6 image.

This is a reproducible binary extension, not a rebuild of the historical C tree.
It changes two verified BL call sites and adds assembly in unused application
flash. The original SoftDevice, bootloader and all other application bytes stay.
"""
import argparse
import hashlib
import json
from pathlib import Path
from keystone import Ks, KS_ARCH_ARM, KS_MODE_THUMB, KS_MODE_LITTLE_ENDIAN
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_LITTLE_ENDIAN, CS_MODE_MCLASS

BASE = 0x26000
EXT = 0x3A400
IDLE_EXT = EXT + 0x100
MAILBOX = 0x2000FFC0
MAGIC = 0x31525346
COOKIE = 0xA6F01602
BOOTSTAMP = 0x31525342
ACTIVE = 0x2000536F
COIL = 0x2000536C
SAVING = 0x20005370
EXPECTED_BIN_SHA256 = '8c6827f0eb0da3cd8c4b092e8368cb9e01746756d26b0aa6d44f2cf2b725c602'
EXPECTED_FULL_SHA256 = '7feae35a573f95cbc75a5cc83dfdf086ceed9f619afa0f101804f756451c11dc'
STEM = 'VGL_Patrol_1.6_IDENTITY_RESET_SERVICE'
FUNCTIONS = {
    'idle': (0x28158, 8, 'aafe07560eeaf57109bbab24192f662cb86e432fb5d168da9c24fc8ca4baff8a'),
    'write_settings_bytes': (0x28B5E, 298, 'def6cba41c338eb909225d650d711035c043594a5a7fc6f86820be179e34aee6'),
    'read_settings_bytes': (0x2675C, 90, '409a0111f13fd3051fe2c977b2bfe80945cadac33f5065406a1425790016fe45'),
    'gpio_event_disable': (0x2E752, 58, '3c790b21f4811cc4f77d7f87daa2305a4e2f71f0f7522a972327930122131c9a'),
    'app_timer_stop': (0x2B86A, 46, '1b6b397439e0373b26a9529fc708a5f33c282971258329b4eb4ecb60dbb9c069'),
    'rf_sleep': (0x2CC02, 12, '065848eaf7e9dec1d1a86471e5de7732236f7f0025eaf6fdd9ad40998476d07b'),
    'app_timer_init_irq_priority': (0x2B790, 76, 'fa32fc3a2427e7963c67dec28112890ac7c1f5313f1d2148ba0900624df83f6f'),
    'gpiote_init_irq_priority': (0x2E564, 124, 'f34d40b1c9738630f347f2597e7e0839990f53e00bcb4fe8f2511f9f532c3fb6'),
}
SECRET = 0x20002A88
SERIAL = 0x20002A98
RF_TIMER = 0x20005168
READ_DELAY_TIMER = 0x200051A8
DEFAULT_SETTINGS = bytes([0]) + b'0'*22

GUARDS = {0x292BC: bytes.fromhex('fff730fd'), 0x292C0: bytes.fromhex('fef74aff')}

def sha256(b):
    return hashlib.sha256(b).hexdigest()

def read_hex(path):
    memory, high, start, eof = {}, 0, None, False
    for number, line in enumerate(Path(path).read_text('ascii').splitlines(), 1):
        if not line:
            continue
        if eof or not line.startswith(':'):
            raise ValueError(f'Invalid HEX line {number}')
        rec = bytes.fromhex(line[1:])
        if len(rec) != rec[0] + 5 or sum(rec) & 255:
            raise ValueError(f'HEX checksum/length error on line {number}')
        address, kind, data = int.from_bytes(rec[1:3], 'big'), rec[3], rec[4:-1]
        if kind == 0:
            for offset, value in enumerate(data):
                a = high + address + offset
                if a in memory and memory[a] != value:
                    raise ValueError(f'Conflicting HEX byte {a:#x}')
                memory[a] = value
        elif kind == 1:
            eof = True
        elif kind == 4:
            high = int.from_bytes(data, 'big') << 16
        elif kind == 2:
            high = int.from_bytes(data, 'big') << 4
        elif kind == 5:
            start = int.from_bytes(data, 'big')
        elif kind == 3:
            raise ValueError('Unexpected segmented start record')
        else:
            raise ValueError(f'Unknown HEX record type {kind}')
    if not eof:
        raise ValueError('HEX EOF missing')
    return memory, start

def hex_record(address, kind, data=b''):
    payload = bytes([len(data)]) + address.to_bytes(2, 'big') + bytes([kind]) + data
    return ':' + (payload + bytes([-sum(payload) & 255])).hex().upper()

def write_hex(path, memory, start=None):
    lines, addresses, index, high = [], sorted(memory), 0, None
    while index < len(addresses):
        a = addresses[index]
        if a >> 16 != high:
            high = a >> 16
            lines.append(hex_record(0, 4, high.to_bytes(2, 'big')))
        values = []
        while index < len(addresses) and len(values) < 16:
            b = addresses[index]
            if b != a + len(values) or b >> 16 != high:
                break
            values.append(memory[b]); index += 1
        lines.append(hex_record(a & 65535, 0, bytes(values)))
    if start is not None:
        lines.append(hex_record(0, 5, start.to_bytes(4, 'big')))
    lines.append(hex_record(0, 1))
    Path(path).write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))

def assemble(source, address):
    # Keystone 0.9.2 omits the Cortex-M system register names. These encodings
    # are MRS/BASEPRI and MSR/BASEPRI(_MAX), checked by Capstone and emulator.
    encodings = {
        'mrs r6, basepri': '.hword 0xf3ef, 0x8611',
        'mrs r7, basepri': '.hword 0xf3ef, 0x8711',
        'msr basepri_max, r0': '.hword 0xf380, 0x8812',
        'msr basepri, r6': '.hword 0xf386, 0x8811',
        'msr basepri, r7': '.hword 0xf387, 0x8811',
    }
    for instruction, encoding in encodings.items():
        source = source.replace(instruction, encoding)
    return bytes(Ks(KS_ARCH_ARM, KS_MODE_THUMB | KS_MODE_LITTLE_ENDIAN).asm(source, address)[0])

def assembly():
    startup = f"""
        push {{r4, r5, r6, lr}}
        ldr r4, ={MAILBOX}
        movs r0, #0
        movs r1, #16
    clear_mailbox:
        str r0, [r4], #4
        subs r1, #1
        bne clear_mailbox
        ldr r4, ={MAILBOX}
        mrs r6, basepri
        movs r0, #192
        msr basepri_max, r0
        dsb
        isb
        ldr r5, ={ACTIVE}
        movs r0, #0
        strb r0, [r5]
        movs r0, #9
        bl 0x2e752
        movs r0, #3
        bl 0x2e752
        ldr r0, ={RF_TIMER}
        bl 0x2b86a
        str r0, [r4, #12]
        cmp r0, #0
        bne startup_failed
        ldr r0, ={READ_DELAY_TIMER}
        bl 0x2b86a
        str r0, [r4, #12]
        cmp r0, #0
        bne startup_failed
        bl 0x2cc02
        b startup_done
    startup_failed:
        movs r0, #230
        str r0, [r4, #8]
    startup_done:
        msr basepri, r6
        dsb
        isb
        ldr r0, ={BOOTSTAMP}
        str r0, [r4, #60]
        pop {{r4, r5, r6, pc}}
        .align 2
        .ltorg
    """
    idle = f"""
        push {{r4, r5, r6, r7, r8, lr}}
        ldr r4, ={MAILBOX}
        ldr r0, [r4]
        ldr r1, ={MAGIC}
        cmp r0, r1
        bne normal_idle
        ldr r0, [r4, #4]
        ldr r1, ={COOKIE}
        cmp r0, r1
        bne normal_idle
        ldr r0, [r4, #8]
        cmp r0, #0
        bne reject_consumed_request
        mrs r7, basepri
        movs r0, #192
        msr basepri_max, r0
        dsb
        isb
        movs r0, #0
        str r0, [r4]
        str r0, [r4, #24]
        str r0, [r4, #20]
        movs r0, #1
        str r0, [r4, #8]
        movs r0, #0
        mvns r0, r0
        str r0, [r4, #12]
        str r0, [r4, #16]
        ldr r5, ={COIL}
        ldrb r0, [r5]
        cmp r0, #0
        bne fail_coil
        ldr r5, ={SAVING}
        ldrb r0, [r5]
        cmp r0, #0
        bne fail_saving
        ldr r5, ={ACTIVE}
        movs r0, #0
        strb r0, [r5]
        adr r5, reset_defaults
        movs r0, #4
        mov r1, r5
        movs r2, #23
        bl 0x28b5e
        str r0, [r4, #12]
        cmp r0, #0
        bne fail_write
        add r5, r4, #32
        movs r6, #23
        movs r0, #165
    prefill_readback:
        strb r0, [r5], #1
        subs r6, #1
        bne prefill_readback
        add r0, r4, #32
        movs r1, #4
        movs r2, #23
        bl 0x2675c
        str r0, [r4, #16]
        cmp r0, #0
        bne fail_read
        add r5, r4, #32
        adr r6, reset_defaults
        movs r8, #23
    compare_readback:
        ldrb r0, [r5], #1
        ldrb r1, [r6], #1
        cmp r0, r1
        bne fail_compare
        subs r8, #1
        bne compare_readback
        ldr r5, ={SECRET}
        movs r6, #22
        movs r0, #48
    reset_ram_identity:
        strb r0, [r5], #1
        subs r6, #1
        bne reset_ram_identity
        movs r0, #23
        str r0, [r4, #20]
        ldr r0, ={COOKIE}
        str r0, [r4, #24]
        movs r0, #2
        b set_status
    fail_write:
        movs r0, #225
        b set_status
    fail_read:
        movs r0, #226
        b set_status
    fail_compare:
        movs r0, #227
        b set_status
    fail_coil:
        movs r0, #228
        b set_status
    fail_saving:
        movs r0, #229
    set_status:
        str r0, [r4, #8]
        msr basepri, r7
        dsb
        isb
        b normal_idle
    reject_consumed_request:
        movs r0, #0
        str r0, [r4]
    normal_idle:
        pop {{r4, r5, r6, r7, r8, lr}}
        b.w 0x28158
        .align 2
    reset_defaults:
        .byte 0,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48
        .align 2
        .ltorg
    """
    return startup, idle

def build(base_dir, output):
    original_bin = (base_dir / 'VGL_Patrol_1.6_1400ms_RF5ms.bin').read_bytes()
    full_path = base_dir / 'VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex'
    if sha256(original_bin) != EXPECTED_BIN_SHA256 or sha256(full_path.read_bytes()) != EXPECTED_FULL_SHA256:
        raise ValueError('Unsupported production baseline. Refusing to patch.')
    memory, start = read_hex(full_path)
    if len(original_bin) != 82874:
        raise ValueError('Wrong production BIN size')
    for name, (address, size, digest) in FUNCTIONS.items():
        if sha256(original_bin[address-BASE:address-BASE+size]) != digest:
            raise ValueError(f'Function signature mismatch: {name}')
    for a, expected in GUARDS.items():
        if original_bin[a-BASE:a-BASE+4] != expected:
            raise ValueError(f'Patch guard mismatch at {a:#x}')
    for a, b in memory.items():
        if BASE <= a < BASE + len(original_bin) and b != original_bin[a-BASE]:
            raise ValueError(f'BIN/HEX loadable-byte mismatch at {a:#x}')

    startup_source, idle_source = assembly()
    startup = assemble(startup_source, EXT)
    idle = assemble(idle_source, IDLE_EXT)
    if len(startup) > IDLE_EXT - EXT:
        raise ValueError('Startup extension overflow')
    extension = startup + bytes.fromhex('00bf') * ((IDLE_EXT-EXT-len(startup))//2) + idle
    if EXT + len(extension) >= 0x3B000:
        raise ValueError('Extension must fit original application final erase sector')
    if any(a in memory for a in range(EXT, EXT + len(extension))):
        raise ValueError('Extension address already occupied')
    candidate = bytearray(original_bin)
    candidate.extend(bytes(EXT - BASE - len(candidate)))
    candidate.extend(extension)
    patches = {0x292BC: assemble(f'bl {EXT}', 0x292BC), 0x292C0: assemble(f'bl {IDLE_EXT}', 0x292C0)}
    for address, data in patches.items():
        if len(data) != 4:
            raise ValueError('BL patch must be exactly four bytes')
        candidate[address-BASE:address-BASE+4] = data
    full = dict(memory)
    for a, data in {**patches, EXT: extension}.items():
        for offset, b in enumerate(data):
            full[a+offset] = b
    for a, b in memory.items():
        if not any(p <= a < p+4 for p in patches) and full[a] != b:
            raise AssertionError('Unexpected production byte change')
    if any(a >= 0x10000000 for a in full):
        raise ValueError('Unexpected UICR/FICR record')
    changed = [BASE+i for i,(a,b) in enumerate(zip(original_bin,candidate)) if a != b]
    if any(not any(p <= a < p+4 for p in patches) for a in changed):
        raise AssertionError('Unexpected BIN byte change')
    output.mkdir(parents=True, exist_ok=True)
    (output / (STEM + '.bin')).write_bytes(candidate)
    write_hex(output / (STEM + '_APP.hex'), {a:b for a,b in full.items() if BASE <= a < 0x78000})
    write_hex(output / (STEM + '_FULL_S132_BOOTLOADER.hex'), full, start)
    (output / 'reset_startup.S').write_text(startup_source, 'ascii')
    (output / 'reset_idle.S').write_text(idle_source, 'ascii')
    dis = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN | CS_MODE_MCLASS)
    listing = ['# Linear disassembly includes literal pools and hard-coded default data.',
               '# Refer to reset_startup.S/reset_idle.S labels to distinguish code and data.']
    for address, data in [(EXT, startup), (IDLE_EXT, idle)]:
        for ins in dis.disasm(data, address):
            listing.append(f'{ins.address:08x}: {ins.bytes.hex():12} {ins.mnemonic:8} {ins.op_str}')
    (output / 'reset_extension.disasm.txt').write_text('\n'.join(listing)+'\n', 'ascii')
    report = {
        'kind':'one_shot_identity_reset_service', 'production_version':'1.6',
        'production_baseline_sha256':EXPECTED_BIN_SHA256,
        'full_baseline_sha256':EXPECTED_FULL_SHA256,
        'application_base':hex(BASE), 'startup_extension':hex(EXT), 'idle_extension':hex(IDLE_EXT),
        'extension_bytes':len(extension), 'mailbox':hex(MAILBOX), 'mailbox_size':64,
        'request_magic':hex(MAGIC), 'request_cookie':hex(COOKIE), 'bootstamp':hex(BOOTSTAMP),
        'external_settings_address':4, 'external_settings_size':23,
        'default_settings_hex':DEFAULT_SETTINGS.hex(),
        'settings_write_entry':hex(0x28B5E), 'settings_read_entry':hex(0x2675C),
        'critical_section':'save BASEPRI; BASEPRI_MAX 0xC0; DSB/ISB; restore on all paths',
        'restore_erases_entire_service_sector':EXT+len(extension)<0x3B000,
        'original_bytes_changed':len(changed),
        'patches':[{'address':hex(a),'before':GUARDS[a].hex(),'after':b.hex()}for a,b in patches.items()],
        'normal_boot':'service startup clears mailbox, suppresses RF/shock/coil; no persistent reset without explicit SWD cookie/magic',
        'external_settings_writes_added':True,
        'external_write_scope':'W25Q offsets 4..26 only; existing checked sector RMW preserves neighbors',
        'external_settings_verified_by_fresh_readback':True,
        'writes_added_to_eeprom_or_rtc':False,
        'uicr_records_added':False,
        'hardware_validated':False,
    }
    (output / 'build_report.json').write_text(json.dumps(report,indent=2)+'\n','ascii')
    return report

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--base-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args=parser.parse_args()
    print(json.dumps(build(args.base_dir,args.output_dir),indent=2))
