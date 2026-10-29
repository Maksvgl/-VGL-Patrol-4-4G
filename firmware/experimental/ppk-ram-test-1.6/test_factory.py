#!/usr/bin/env python3
"""Host tests and Cortex-M Thumb emulation of the new extension only.

Existing production peripheral routines are mocked at their verified entries.
These tests do not claim to emulate a physical nRF52832, I2C chip or PPK2.
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
import build_factory as b

HERE=Path(__file__).resolve().parent
def find_baseline():
    for parent in [HERE, *HERE.parents]:
        for relative in ['firmware/releases/v1.6-1400ms-rf5ms',
                         'patrol_current_release/firmware/releases/v1.6-1400ms-rf5ms']:
            path=parent/relative
            if (path/'VGL_Patrol_1.6_1400ms_RF5ms.bin').is_file():
                return path
    raise RuntimeError('Set VGL_PPK_BASE_DIR to the approved production package')

BASE_DIR=Path(os.environ['VGL_PPK_BASE_DIR']) if 'VGL_PPK_BASE_DIR' in os.environ else find_baseline()
DIST=Path(os.environ.get('VGL_PPK_DIST_DIR',str(HERE/'dist')))

class Emulator:
    def __init__(self, writeresult=0, readresult=0):
        self.uc=Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(0,0x100000)
        self.uc.mem_map(0x20000000,0x10000)
        image=(DIST/(b.STEM+'.bin')).read_bytes()
        self.uc.mem_write(b.BASE,image)
        self.uc.reg_write(UC_ARM_REG_SP,0x20007000)
        self.uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
        self.calls=[]
        self.clock=[0,0,12,29,10,26]
        self.writeresult,self.readresult=writeresult,readresult
        self.stop=None
        self.saved={}
        for i,r in enumerate([UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]):
            self.saved[r]=0x11110000+i
            self.uc.reg_write(r,self.saved[r])
        self.uc.hook_add(UC_HOOK_CODE,self.hook)

    def word(self,address):
        return int.from_bytes(self.uc.mem_read(address,4),'little')

    def setword(self,address,value):
        self.uc.mem_write(address,struct.pack('<I',value))

    def args(self):
        u=self.uc
        return [u.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]]+[self.word(u.reg_read(UC_ARM_REG_SP)),self.word(u.reg_read(UC_ARM_REG_SP)+4)]

    def hook(self,u,address,size,data):
        if address==self.stop:
            u.emu_stop();return
        if address not in [0x267FA,0x26A72,0x28D20,0x28158]:
            return
        if u.reg_read(UC_ARM_REG_SP)%8:
            raise AssertionError('AAPCS stack alignment broken')
        self.calls.append(address)
        if address==0x267FA:
            values=self.args()
            if values!=self.clock:
                raise AssertionError(f'Wrong writeClock argument ABI: {values}')
            u.reg_write(UC_ARM_REG_R0,self.writeresult)
        elif address==0x26A72:
            if self.readresult==0:
                for ptr,value in zip(self.args(),self.clock):
                    if not b.MAILBOX+24<=ptr<=b.MAILBOX+29:
                        raise AssertionError('readClock pointer outside result mailbox')
                    u.mem_write(ptr,bytes([value]))
            u.reg_write(UC_ARM_REG_R0,self.readresult)
        u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))

    def request(self, valid=True):
        self.setword(b.MAILBOX,b.MAGIC)
        self.setword(b.MAILBOX+4,b.COOKIE if valid else b.COOKIE^1)
        self.uc.mem_write(b.MAILBOX+32,bytes(self.clock))

    def run(self,address,end):
        self.stop=end
        self.uc.emu_start(address|1,0,count=10000)
        if self.uc.reg_read(UC_ARM_REG_PC)!=end:
            raise AssertionError('Extension did not return to expected production site')
        for r,value in self.saved.items():
            if self.uc.reg_read(r)!=value:
                raise AssertionError(f'Callee-saved register clobbered: {r}')
        if self.uc.reg_read(UC_ARM_REG_SP)!=0x20007000:
            raise AssertionError('Stack not restored')

class ExtensionTests(unittest.TestCase):
    def test_startup_clears_entire_volatile_mailbox(self):
        e=Emulator();e.uc.mem_write(b.MAILBOX,b'\xA5'*64)
        e.run(0x292BC,0x292C0)
        self.assertEqual(bytes(e.uc.mem_read(b.MAILBOX,60)),bytes(60))
        self.assertEqual(e.word(b.MAILBOX+60),b.BOOTSTAMP)
        self.assertEqual(e.calls,[0x28D20])
        self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\0')

    def test_normal_idle_has_no_factory_side_effects(self):
        e=Emulator();before=bytes(e.uc.mem_read(b.MAILBOX,64))
        e.run(0x292C0,0x292C4)
        self.assertEqual(e.calls,[0x28158])
        self.assertEqual(bytes(e.uc.mem_read(b.MAILBOX,64)),before)
        self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\0')

    def test_wrong_cookie_does_not_activate(self):
        e=Emulator();e.request(False);e.run(0x292C0,0x292C4)
        self.assertEqual(e.calls,[0x28158])
        self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\0')

    def test_success_writes_rtc_and_activates_only_ram(self):
        e=Emulator();e.request()
        identity=bytes(range(24));e.uc.mem_write(0x20002A88,identity)
        e.run(0x292C0,0x292C4)
        self.assertEqual(e.calls,[0x267FA,0x26A72,0x28D20,0x28158])
        self.assertEqual(e.word(b.MAILBOX),0)
        self.assertEqual(e.word(b.MAILBOX+8),2)
        self.assertEqual(e.word(b.MAILBOX+40),b.COOKIE)
        self.assertEqual(e.word(b.MAILBOX+12),0)
        self.assertEqual(e.word(b.MAILBOX+16),0)
        self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\1')
        self.assertEqual(e.word(b.PERIOD),b.DELAY)
        self.assertEqual(e.word(b.MAILBOX+20),b.DELAY)
        self.assertEqual(bytes(e.uc.mem_read(b.MAILBOX+24,6)),bytes(e.clock))
        self.assertEqual(bytes(e.uc.mem_read(0x20002A88,24)),identity)

    def test_request_runs_only_once(self):
        e=Emulator();e.request();e.run(0x292C0,0x292C4)
        e.calls=[];e.run(0x292C0,0x292C4)
        self.assertEqual(e.calls,[0x28158])

    def test_rtc_write_failure_does_not_activate(self):
        e=Emulator(writeresult=1);e.request();e.run(0x292C0,0x292C4)
        self.assertEqual(e.word(b.MAILBOX+8),0xE1)
        self.assertEqual(e.calls,[0x267FA,0x28158])
        self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\0')
        self.assertEqual(e.word(b.MAILBOX+40),0)

    def test_rtc_read_failure_does_not_activate(self):
        for result in [1,2]:
            e=Emulator(readresult=result);e.request();e.run(0x292C0,0x292C4)
            self.assertEqual(e.word(b.MAILBOX+8),0xE2)
            self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\0')
            self.assertNotIn(0x28D20,e.calls)

    def test_coil_and_power_saving_are_rejected(self):
        for flag,status in [(b.COIL,0xE3),(b.SAVING,0xE4)]:
            e=Emulator();e.request();e.uc.mem_write(flag,b'\1')
            e.run(0x292C0,0x292C4)
            self.assertEqual(e.word(b.MAILBOX+8),status)
            self.assertEqual(e.calls,[0x28158])
            self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\0')

    def test_invalid_clock_fields_are_rejected(self):
        for index,value in [(0,60),(1,60),(2,24),(3,0),(3,32),(4,0),(4,13),(5,100)]:
            e=Emulator();e.request();e.uc.mem_write(b.MAILBOX+32+index,bytes([value]))
            e.run(0x292C0,0x292C4)
            self.assertEqual(e.word(b.MAILBOX+8),0xE5)
            self.assertEqual(e.calls,[0x28158])

    def test_normal_reset_discards_test_request_and_result(self):
        e=Emulator();e.request();e.run(0x292C0,0x292C4)
        # Simulate C startup loading the inactive flag from external settings.
        e.uc.mem_write(b.ACTIVE,b'\0');e.calls=[]
        e.run(0x292BC,0x292C0);e.calls=[]
        e.run(0x292C0,0x292C4)
        self.assertEqual(e.word(b.MAILBOX+8),0)
        self.assertEqual(bytes(e.uc.mem_read(b.ACTIVE,1)),b'\0')
        self.assertEqual(e.calls,[0x28158])

class ImageTests(unittest.TestCase):
    def test_only_two_existing_call_sites_changed(self):
        old=(BASE_DIR/'VGL_Patrol_1.6_1400ms_RF5ms.bin').read_bytes()
        new=(DIST/(b.STEM+'.bin')).read_bytes()
        changed=[b.BASE+i for i,(a,c) in enumerate(zip(old,new)) if a!=c]
        self.assertEqual(len(changed),8)
        self.assertTrue(all(any(p<=a<p+4 for p in b.GUARDS)for a in changed))
        self.assertEqual(old[:8],new[:8])

    def test_full_hex_preserves_softdevice_and_bootloader(self):
        old,start=b.read_hex(BASE_DIR/'VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex')
        new,newstart=b.read_hex(DIST/(b.STEM+'_FULL_S132_BOOTLOADER.hex'))
        self.assertEqual(start,newstart)
        for a,value in old.items():
            if a<b.BASE or a>=0x78000:
                self.assertEqual(new[a],value)
        self.assertFalse(any(a>=0x10000000 for a in new))

    def test_bin_app_hex_and_full_are_consistent(self):
        binary=(DIST/(b.STEM+'.bin')).read_bytes()
        app,_=b.read_hex(DIST/(b.STEM+'_APP.hex'))
        full,_=b.read_hex(DIST/(b.STEM+'_FULL_S132_BOOTLOADER.hex'))
        for a,value in app.items():
            self.assertEqual(binary[a-b.BASE],value)
            self.assertEqual(full[a],value)

    def test_build_is_deterministic(self):
        with tempfile.TemporaryDirectory()as directory:
            target=Path(directory);b.build(BASE_DIR,target)
            for suffix in ['.bin','_APP.hex','_FULL_S132_BOOTLOADER.hex']:
                self.assertEqual((DIST/(b.STEM+suffix)).read_bytes(),(target/(b.STEM+suffix)).read_bytes())

    def test_wrong_baseline_is_refused(self):
        with tempfile.TemporaryDirectory()as directory:
            base=Path(directory)/'base';base.mkdir()
            for name in ['VGL_Patrol_1.6_1400ms_RF5ms.bin','VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex']:
                shutil.copyfile(BASE_DIR/name,base/name)
            path=base/'VGL_Patrol_1.6_1400ms_RF5ms.bin';data=bytearray(path.read_bytes());data[100]^=1;path.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'Unsupported production baseline'):
                b.build(base,Path(directory)/'out')

if __name__=='__main__':
    unittest.main(verbosity=2)
