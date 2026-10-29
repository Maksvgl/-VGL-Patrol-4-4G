#!/usr/bin/env python3
"""Execute emitted service Thumb code, with verified production calls mocked.

This tests ABI, critical section and fail-closed reset logic, not a real nRF,
SoftDevice, W25Q bus, application IRQ dispatch or electrical behavior.
"""
import hashlib
import json
import os
import shutil
import struct
import tempfile
import unittest
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import *
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
import build_reset as b

HERE=Path(__file__).resolve().parent
def find_baseline():
    for parent in [HERE,*HERE.parents]:
        for relative in ['firmware/releases/v1.6-1400ms-rf5ms',
                         'patrol_current_release/firmware/releases/v1.6-1400ms-rf5ms']:
            path=parent/relative
            if (path/'VGL_Patrol_1.6_1400ms_RF5ms.bin').is_file():return path
    raise RuntimeError('Set VGL_RESET_BASE_DIR to approved production files')
BASE_DIR=Path(os.environ['VGL_RESET_BASE_DIR'])if'VGL_RESET_BASE_DIR'in os.environ else find_baseline()
DIST=Path(os.environ.get('VGL_RESET_DIST_DIR',str(HERE/'dist')))
CALLS={name:address for name,(address,size,digest)in b.FUNCTIONS.items()
       if name not in ('app_timer_init_irq_priority','gpiote_init_irq_priority')}

class Emulator:
    def __init__(self,basepri=0,write_result=0,read_result=0,read_mode='normal',stop_results=None):
        self.uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        self.uc.mem_map(0,0x100000);self.uc.mem_map(0x20000000,0x10000)
        self.uc.mem_write(b.BASE,(DIST/(b.STEM+'.bin')).read_bytes())
        self.uc.reg_write(UC_ARM_REG_SP,0x20007000)
        self.uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
        self.uc.reg_write(UC_ARM_REG_BASEPRI,basepri)
        self.initial_basepri=basepri
        self.critical=min(basepri or 0xC0,0xC0)
        self.calls=[];self.call_args=[];self.stop=None
        self.write_result=write_result;self.read_result=read_result;self.read_mode=read_mode
        self.stop_results=list(stop_results or [0,0])
        self.external=bytearray((i*17+31)&255 for i in range(4096))
        self.external_before=bytes(self.external)
        self.uc.mem_write(b.SECRET,b'OLDSECRET1234567OLDNUM')
        self.uc.mem_write(b.ACTIVE,b'\x01')
        self.saved={}
        for i,r in enumerate([UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
                              UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]):
            self.saved[r]=0x11110000+i;self.uc.reg_write(r,self.saved[r])
        self.uc.hook_add(UC_HOOK_CODE,self.hook)

    def word(self,address):return int.from_bytes(self.uc.mem_read(address,4),'little')
    def setword(self,address,value):self.uc.mem_write(address,struct.pack('<I',value))
    def hook(self,u,address,size,data):
        if address==self.stop:u.emu_stop();return
        if address not in CALLS.values():return
        if u.reg_read(UC_ARM_REG_SP)%8:raise AssertionError('AAPCS stack alignment')
        regs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]
        args=[u.reg_read(r)for r in regs]
        self.calls.append(address);self.call_args.append((address,args))
        current=u.reg_read(UC_ARM_REG_BASEPRI)
        if address==CALLS['idle']:
            if current!=self.initial_basepri:raise AssertionError('BASEPRI not restored before ordinary idle')
            result=0
        else:
            if current!=self.critical:raise AssertionError(f'Unprotected production call {address:#x}: BASEPRI={current:#x}')
            result=0
            if address==CALLS['app_timer_stop']:
                if args[0]not in(b.RF_TIMER,b.READ_DELAY_TIMER):raise AssertionError('Wrong timer control block')
                result=self.stop_results.pop(0)if self.stop_results else 0
            elif address==CALLS['gpio_event_disable']:
                if args[0]not in(3,9):raise AssertionError('Wrong GPIOTE pin')
            elif address==CALLS['write_settings_bytes']:
                if args[0]!=4 or args[2]!=23:raise AssertionError('Wrong persistent write bounds')
                payload=bytes(u.mem_read(args[1],23))
                if payload!=b.DEFAULT_SETTINGS:raise AssertionError('Wrong hard-coded defaults or source pointer')
                if b.MAILBOX<=args[1]<b.MAILBOX+64:raise AssertionError('Write source aliases host/result mailbox')
                result=self.write_result
                if result==0:self.external[4:27]=payload
            elif address==CALLS['read_settings_bytes']:
                if args[:3]!=[b.MAILBOX+32,4,23]:raise AssertionError('Wrong readback ABI')
                if bytes(u.mem_read(args[0],23))!=b'\xA5'*23:raise AssertionError('Readback destination was not freshly poisoned')
                result=self.read_result
                if result==0:
                    payload=bytes(self.external[4:27])
                    if self.read_mode=='all_zero':payload=bytes(23)
                    elif self.read_mode=='all_ff':payload=b'\xff'*23
                    elif self.read_mode=='wrong_byte':payload=payload[:12]+b'1'+payload[13:]
                    elif self.read_mode=='partial':payload=payload[:12]
                    elif self.read_mode=='untouched':payload=b''
                    u.mem_write(args[0],payload)
        # Model caller-saved registers as clobbered by each production call.
        for r in [UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R12]:u.reg_write(r,0xdeadbeef)
        u.reg_write(UC_ARM_REG_R0,result)
        u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))

    def request(self,cookie=None,magic=None):
        self.setword(b.MAILBOX,b.MAGIC if magic is None else magic)
        self.setword(b.MAILBOX+4,b.COOKIE if cookie is None else cookie)
    def run(self,address,end):
        self.stop=end;self.uc.emu_start(address|1,0,count=20000)
        if self.uc.reg_read(UC_ARM_REG_PC)!=end:raise AssertionError('Unexpected return address')
        if self.uc.reg_read(UC_ARM_REG_SP)!=0x20007000:raise AssertionError('Stack not restored')
        for reg,value in self.saved.items():
            if self.uc.reg_read(reg)!=value:raise AssertionError('Callee-saved register clobbered')
        if self.uc.reg_read(UC_ARM_REG_BASEPRI)!=self.initial_basepri:raise AssertionError('BASEPRI not restored')
    def startup(self):self.run(0x292BC,0x292C0)
    def idle(self):self.run(0x292C0,0x292C4)

class ServiceTests(unittest.TestCase):
    def test_startup_clears_mailbox_suppresses_rf_without_persistent_write(self):
        e=Emulator();e.uc.mem_write(b.MAILBOX,b'\xAA'*64);identity=bytes(e.uc.mem_read(b.SECRET,22));e.startup()
        self.assertEqual(bytes(e.uc.mem_read(b.MAILBOX,60)),bytes(60));self.assertEqual(e.word(b.MAILBOX+60),b.BOOTSTAMP)
        self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\0');self.assertEqual(bytes(e.uc.mem_read(b.SECRET,22)),identity)
        self.assertEqual(e.calls,[CALLS['gpio_event_disable']]*2+[CALLS['app_timer_stop']]*2+[CALLS['rf_sleep']])
        self.assertEqual(bytes(e.external),e.external_before)
        self.assertEqual([a[1][0]for a in e.call_args[:4]],[9,3,b.RF_TIMER,b.READ_DELAY_TIMER])

    def test_startup_preserves_prior_interrupt_mask(self):
        for prior in [0,0x80,0xC0,0xE0]:
            e=Emulator(basepri=prior);e.startup()
            self.assertEqual(e.uc.reg_read(UC_ARM_REG_BASEPRI),prior)

    def test_startup_stop_failure_cannot_clear_identity(self):
        for stops in [[4,0],[0,4]]:
            e=Emulator(stop_results=stops);e.startup();self.assertEqual(e.word(b.MAILBOX+8),0xE6)
            e.request();e.idle();self.assertEqual(e.word(b.MAILBOX+8),0xE6);self.assertEqual(e.word(b.MAILBOX),0)
            self.assertNotIn(CALLS['write_settings_bytes'],e.calls);self.assertEqual(bytes(e.external),e.external_before)

    def test_normal_idle_never_writes_identity(self):
        e=Emulator();e.startup();e.calls=[];before=bytes(e.uc.mem_read(b.MAILBOX,64));e.idle()
        self.assertEqual(e.calls,[CALLS['idle']]);self.assertEqual(bytes(e.uc.mem_read(b.MAILBOX,64)),before)
        self.assertEqual(bytes(e.external),e.external_before)

    def test_wrong_cookie_or_magic_never_writes(self):
        for cookie,magic in [(b.COOKIE^1,b.MAGIC),(b.COOKIE,b.MAGIC^1)]:
            e=Emulator();e.startup();e.calls=[];e.request(cookie,magic);e.idle()
            self.assertEqual(e.calls,[CALLS['idle']]);self.assertEqual(bytes(e.external),e.external_before)

    def test_success_has_exact_scoped_write_fresh_verify_and_ram_defaults(self):
        e=Emulator();e.startup();e.calls=[];e.request();e.idle()
        self.assertEqual(e.calls,[CALLS['write_settings_bytes'],CALLS['read_settings_bytes'],CALLS['idle']])
        self.assertEqual(e.word(b.MAILBOX),0);self.assertEqual(e.word(b.MAILBOX+8),2)
        self.assertEqual(e.word(b.MAILBOX+12),0);self.assertEqual(e.word(b.MAILBOX+16),0)
        self.assertEqual(e.word(b.MAILBOX+20),23);self.assertEqual(e.word(b.MAILBOX+24),b.COOKIE)
        self.assertEqual(bytes(e.uc.mem_read(b.MAILBOX+32,23)),b.DEFAULT_SETTINGS)
        self.assertEqual(bytes(e.uc.mem_read(b.SECRET,22)),b'0'*22)
        self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\0')
        self.assertEqual(e.external[4:27],b.DEFAULT_SETTINGS)
        self.assertEqual(e.external[:4],e.external_before[:4]);self.assertEqual(e.external[27:],e.external_before[27:])

    def test_success_and_every_failure_restore_all_prior_basepri_values(self):
        modes=[({},2),({'write_result':4},0xE1),({'read_result':4},0xE2),({'read_mode':'wrong_byte'},0xE3)]
        for prior in [0,0x80,0xC0,0xE0]:
            for options,status in modes:
                e=Emulator(basepri=prior,**options);e.startup();e.request();e.idle()
                self.assertEqual(e.word(b.MAILBOX+8),status)
                self.assertEqual(e.uc.reg_read(UC_ARM_REG_BASEPRI),prior)

    def test_coil_and_power_saving_gate_before_write(self):
        for flag,status in [(b.COIL,0xE4),(b.SAVING,0xE5)]:
            for prior in [0,0x80]:
                e=Emulator(basepri=prior);e.startup();e.calls=[];e.uc.mem_write(flag,b'\1');e.request();e.idle()
                self.assertEqual(e.word(b.MAILBOX+8),status);self.assertEqual(e.calls,[CALLS['idle']])
                self.assertEqual(bytes(e.external),e.external_before);self.assertEqual(e.word(b.MAILBOX+24),0)

    def test_write_error_never_reads_or_acknowledges(self):
        e=Emulator(write_result=4);e.startup();e.calls=[];e.request();e.idle()
        self.assertEqual(e.word(b.MAILBOX+8),0xE1);self.assertEqual(e.word(b.MAILBOX+12),4)
        self.assertEqual(e.word(b.MAILBOX+16),0xffffffff);self.assertEqual(e.word(b.MAILBOX+24),0)
        self.assertNotIn(CALLS['read_settings_bytes'],e.calls)

    def test_read_error_never_acknowledges(self):
        e=Emulator(read_result=4);e.startup();e.request();e.idle()
        self.assertEqual(e.word(b.MAILBOX+8),0xE2);self.assertEqual(e.word(b.MAILBOX+16),4)
        self.assertEqual(e.word(b.MAILBOX+20),0);self.assertEqual(e.word(b.MAILBOX+24),0)

    def test_corrupt_stuck_or_partial_readback_never_acknowledges(self):
        for mode in ['all_zero','all_ff','wrong_byte','partial','untouched']:
            e=Emulator(read_mode=mode);e.startup();e.request();e.idle()
            self.assertEqual(e.word(b.MAILBOX+8),0xE3);self.assertEqual(e.word(b.MAILBOX+24),0)

    def test_request_is_one_shot_even_if_host_repeats_magic(self):
        for options in [{},{'write_result':4},{'read_mode':'all_zero'}]:
            e=Emulator(**options);e.startup();e.request();e.idle();status=e.word(b.MAILBOX+8)
            e.calls=[];e.request();e.idle();self.assertEqual(e.calls,[CALLS['idle']])
            self.assertEqual(e.word(b.MAILBOX+8),status);self.assertEqual(e.word(b.MAILBOX),0)

class ImageTests(unittest.TestCase):
    def test_only_original_two_bl_sites_change(self):
        old=(BASE_DIR/'VGL_Patrol_1.6_1400ms_RF5ms.bin').read_bytes();new=(DIST/(b.STEM+'.bin')).read_bytes()
        changed=[b.BASE+i for i,(a,c)in enumerate(zip(old,new))if a!=c]
        self.assertEqual(len(changed),8);self.assertTrue(all(any(p<=a<p+4 for p in b.GUARDS)for a in changed));self.assertEqual(old[:8],new[:8])

    def test_softdevice_bootloader_preserved_and_no_uicr(self):
        old,start=b.read_hex(BASE_DIR/'VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex');new,s=b.read_hex(DIST/(b.STEM+'_FULL_S132_BOOTLOADER.hex'))
        self.assertEqual(s,start)
        for a,value in old.items():
            if a<b.BASE or a>=0x78000:self.assertEqual(new[a],value)
        self.assertFalse(any(a>=0x10000000 for a in new))

    def test_service_tail_is_within_stock_final_erase_sector(self):
        report=json.loads((DIST/'build_report.json').read_text());self.assertLess(b.EXT+report['extension_bytes'],0x3B000)
        self.assertTrue(report['restore_erases_entire_service_sector'])

    def test_bin_app_full_consistent(self):
        binary=(DIST/(b.STEM+'.bin')).read_bytes();app,_=b.read_hex(DIST/(b.STEM+'_APP.hex'));full,_=b.read_hex(DIST/(b.STEM+'_FULL_S132_BOOTLOADER.hex'))
        for a,value in app.items():self.assertEqual(binary[a-b.BASE],value);self.assertEqual(full[a],value)

    def test_basepri_instruction_encodings_decode_as_intended(self):
        cs=Cs(CS_ARCH_ARM,CS_MODE_THUMB|CS_MODE_MCLASS)
        for source in ['mrs r6, basepri','mrs r7, basepri','msr basepri_max, r0','msr basepri, r6','msr basepri, r7']:
            ins=list(cs.disasm(b.assemble(source,0),0));self.assertEqual(len(ins),1)
            self.assertEqual(ins[0].mnemonic+' '+ins[0].op_str,source)

    def test_build_is_deterministic(self):
        with tempfile.TemporaryDirectory()as d:
            target=Path(d);b.build(BASE_DIR,target)
            for suffix in ['.bin','_APP.hex','_FULL_S132_BOOTLOADER.hex']:
                self.assertEqual((DIST/(b.STEM+suffix)).read_bytes(),(target/(b.STEM+suffix)).read_bytes())

    def test_wrong_baseline_is_refused(self):
        with tempfile.TemporaryDirectory()as d:
            base=Path(d)/'base';base.mkdir()
            for name in ['VGL_Patrol_1.6_1400ms_RF5ms.bin','VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex']:shutil.copyfile(BASE_DIR/name,base/name)
            p=base/'VGL_Patrol_1.6_1400ms_RF5ms.bin';data=bytearray(p.read_bytes());data[100]^=1;p.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'Unsupported production baseline'):b.build(base,Path(d)/'out')

if __name__=='__main__':unittest.main(verbosity=2)
