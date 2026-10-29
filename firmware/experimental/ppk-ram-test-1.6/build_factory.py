#!/usr/bin/env python3
"""Build the volatile PPK extension for ONE hash-locked VGL Patrol 1.6 image.

This is a reproducible binary extension, not a rebuild of the historical C tree.
It changes two verified BL call sites and adds assembly in unused application
flash. The original SoftDevice, bootloader and all other application bytes stay.
"""
import argparse
import hashlib
import json
from pathlib import Path
from keystone import Ks, KS_ARCH_ARM, KS_MODE_THUMB, KS_MODE_LITTLE_ENDIAN
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_LITTLE_ENDIAN

BASE = 0x26000
EXT = 0x3A400
IDLE_EXT = EXT + 0x80
MAILBOX = 0x2000FFC0
MAGIC = 0x31544B50
COOKIE = 0xA6F01601
BOOTSTAMP = 0x31545046
DELAY = 45875
ACTIVE = 0x2000536F
COIL = 0x2000536C
SAVING = 0x20005370
PERIOD = 0x20005320
EXPECTED_BIN_SHA256 = '8c6827f0eb0da3cd8c4b092e8368cb9e01746756d26b0aa6d44f2cf2b725c602'
EXPECTED_FULL_SHA256 = '7feae35a573f95cbc75a5cc83dfdf086ceed9f619afa0f101804f756451c11dc'
STEM = 'VGL_Patrol_1.6_PPK_RAM_TEST'
FUNCTIONS = {
    'idle': (0x28158, 8, 'aafe07560eeaf57109bbab24192f662cb86e432fb5d168da9c24fc8ca4baff8a'),
    'restart': (0x28D20, 52, '22cafc303389fb671638f2d8deffef528dcb24e5c08c7e5fe773a83cebdac2c6'),
    'write_clock': (0x267FA, 460, '3fb503750de78d3ebaf6a9b7bcfa1d22c2d04e1e8c3f4c48c2fbea220fc5698c'),
    'read_clock': (0x26A72, 364, 'ba5cf1a162e83dab939c00711eda49921f04b39e8a3bd3fb778e6763dbba8618'),
}
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
    return bytes(Ks(KS_ARCH_ARM, KS_MODE_THUMB | KS_MODE_LITTLE_ENDIAN).asm(source, address)[0])

def assembly():
    startup = f'''
        push {{r4, lr}}
        ldr r4, ={MAILBOX}
        movs r0, #0
        movs r1, #16
    clear_mailbox:
        str r0, [r4], #4
        subs r1, #1
        bne clear_mailbox
        ldr r4, ={MAILBOX}
        ldr r0, ={BOOTSTAMP}
        str r0, [r4, #60]
        bl 0x28d20
        pop {{r4, pc}}
        .align 2
        .ltorg
    '''
    idle = f'''
        push {{r4, r5, r6, lr}}
        sub sp, #8
        ldr r4, ={MAILBOX}
        ldr r0, [r4]
        ldr r1, ={MAGIC}
        cmp r0, r1
        bne normal_idle
        ldr r0, [r4, #4]
        ldr r1, ={COOKIE}
        cmp r0, r1
        bne normal_idle

        movs r0, #0
        str r0, [r4]
        str r0, [r4, #40]
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

        ldrb r0, [r4, #32]
        cmp r0, #59
        bhi fail_date
        ldrb r0, [r4, #33]
        cmp r0, #59
        bhi fail_date
        ldrb r0, [r4, #34]
        cmp r0, #23
        bhi fail_date
        ldrb r0, [r4, #35]
        cmp r0, #1
        blo fail_date
        cmp r0, #31
        bhi fail_date
        ldrb r0, [r4, #36]
        cmp r0, #1
        blo fail_date
        cmp r0, #12
        bhi fail_date
        ldrb r0, [r4, #37]
        cmp r0, #99
        bhi fail_date

        ldrb r0, [r4, #36]
        str r0, [sp]
        ldrb r0, [r4, #37]
        str r0, [sp, #4]
        ldrb r0, [r4, #32]
        ldrb r1, [r4, #33]
        ldrb r2, [r4, #34]
        ldrb r3, [r4, #35]
        bl 0x267fa
        str r0, [r4, #12]
        cmp r0, #0
        bne fail_write

        add r0, r4, #28
        str r0, [sp]
        add r0, r4, #29
        str r0, [sp, #4]
        add r0, r4, #24
        add r1, r4, #25
        add r2, r4, #26
        add r3, r4, #27
        bl 0x26a72
        str r0, [r4, #16]
        cmp r0, #0
        bne fail_read

        ldr r5, ={ACTIVE}
        movs r0, #1
        strb r0, [r5]
        ldr r5, ={PERIOD}
        movw r0, #{DELAY}
        str r0, [r5]
        str r0, [r4, #20]
        bl 0x28d20
        ldr r0, ={COOKIE}
        str r0, [r4, #40]
        movs r0, #2
        b set_status
    fail_write:
        movs r0, #225
        b set_status
    fail_read:
        movs r0, #226
        b set_status
    fail_coil:
        movs r0, #227
        b set_status
    fail_saving:
        movs r0, #228
        b set_status
    fail_date:
        movs r0, #229
    set_status:
        str r0, [r4, #8]
    normal_idle:
        add sp, #8
        pop {{r4, r5, r6, lr}}
        b.w 0x28158
        .align 2
        .ltorg
    '''
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
    if len(startup) > 0x80:
        raise ValueError('Startup extension overflow')
    extension = startup + bytes.fromhex('00bf') * ((0x80-len(startup))//2) + idle
    if EXT + len(extension) >= 0x78000:
        raise ValueError('Extension overlaps bootloader')
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
    (output / 'factory_startup.S').write_text(startup_source, 'ascii')
    (output / 'factory_idle.S').write_text(idle_source, 'ascii')
    dis = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    listing = []
    for address, data in [(EXT, startup), (IDLE_EXT, idle)]:
        for ins in dis.disasm(data, address):
            listing.append(f'{ins.address:08x}: {ins.bytes.hex():12} {ins.mnemonic:8} {ins.op_str}')
    (output / 'factory_extension.disasm.txt').write_text('\n'.join(listing)+'\n', 'ascii')
    report = {
        'kind':'experimental_volatile_ppk_extension', 'production_version':'1.6',
        'production_baseline_sha256':EXPECTED_BIN_SHA256,
        'full_baseline_sha256':EXPECTED_FULL_SHA256,
        'application_base':hex(BASE), 'startup_extension':hex(EXT), 'idle_extension':hex(IDLE_EXT),
        'extension_bytes':len(extension), 'mailbox':hex(MAILBOX), 'mailbox_size':64,
        'request_magic':hex(MAGIC), 'request_cookie':hex(COOKIE), 'bootstamp':hex(BOOTSTAMP),
        'period_ticks':DELAY, 'period_ms':1400, 'rf_startup_delay_ms':5,
        'original_bytes_changed':len(changed),
        'patches':[{'address':hex(a),'before':GUARDS[a].hex(),'after':b.hex()}for a,b in patches.items()],
        'normal_boot':'mailbox cleared before initial reader start; no automatic test activation',
        'external_settings_writes_added':False,
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
