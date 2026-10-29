#!/usr/bin/env python3
import json,os,sys,re,struct,datetime
from pathlib import Path
args=sys.argv[1:]
sp=Path(os.environ['MOCK_STATE_PATH']);tp=Path(os.environ['MOCK_TRACE_PATH']);scenario=os.environ.get('MOCK_SCENARIO','')
s=json.loads(sp.read_text())if sp.exists()else {'uicr':0x78000,'memory':{},'firmware':'VGL_Patrol_1.6_PPK_RAM_TEST_FULL_S132_BOOTLOADER.hex'}
mem=s['memory']
def put(addr,data):
 for i,b in enumerate(data):mem[str(addr+i)]=b
def get(addr,n):return bytes(mem.get(str(addr+i),0)for i in range(n))
def word(addr):return int.from_bytes(get(addr,4),'little')
def pw(addr,v):put(addr,struct.pack('<I',v))
def boot():
 put(0x2000ffc0,bytes(64));pw(0x2000fffc,0x31545046);put(0x2000536c,bytes(8));put(0x2000536f,bytes([1 if scenario=='normal_active'else 0]));pw(0x20005320,45875);pw(0xe000e100,0x100000)
 if scenario=='irq_disabled':pw(0xe000e100,0)
 if scenario=='missing_stamp':pw(0x2000fffc,0)
 if scenario=='stale_mailbox':pw(0x2000ffcc,1)
 if scenario=='running_request':pw(0x2000ffc8,1)
if not mem:boot()
with tp.open('a')as f:f.write(json.dumps({'tool':Path(sys.argv[0]).name,'args':args})+'\n')
if Path(sys.argv[0]).name=='nrfjprog.exe':
 if '--ids'in args:
  print('123456789');
  if scenario=='multiple':print('987654321')
 elif '--recover'in args:s['uicr']=0xffffffff;s['firmware']=None;boot()
 elif '--memrd'in args:
  value=0x12345678 if scenario=='unexpected_uicr'else s['uicr']
  if scenario=='locked':print('Access protection is enabled');sys.exit(1)
  print('0x10001014: %08X'%value)
 elif '--memwr'in args:s['uicr']=int(args[args.index('--val')+1],0)
 elif '--program'in args:s['firmware']=Path(args[args.index('--program')+1]).name;print('Programming verified')
 elif '--reset'in args:boot();print('Reset complete')
 elif '--verify'in args:
  if scenario=='wrong_installed':print('Verification failed');sys.exit(1)
  print('Verified')
 else:raise RuntimeError('Unexpected nrf args '+repr(args))
else:
 script=Path(args[args.index('-CommandFile')+1]);lines=script.read_text().splitlines()
 if scenario=='jlink_error'and 'Release_debug_power_FINAL'in script.name:print('Unknown command: WriteDP');sys.exit(0)
 for line in lines:
  with tp.open('a')as f:f.write(json.dumps({'command':line})+'\n')
  parts=re.split('[,\\s]+',line.strip());parts=[x for x in parts if x]
  op=parts[0].lower()
  if op in ['w4','w1']:
   a=int(parts[1],0);v=int(parts[2],0);put(a,v.to_bytes(4 if op=='w4'else 1,'little'))
  elif op=='go':
   if word(0x2000ffc0)==0x31544b50 and word(0x2000ffc4)==0xa6f01601 and word(0xe000e200)&0x100000:
    pw(0x2000ffc0,0);pw(0x2000ffcc,0);pw(0x2000ffd0,2 if scenario=='rtc_failure'else 0)
    pw(0x2000ffc8,226 if scenario=='rtc_failure'else 2)
    if scenario!='rtc_failure':
     put(0x2000ffd8,get(0x2000ffe0,6));pw(0x2000ffd4,45875);pw(0x2000ffe8,0xa6f01601);put(0x2000536f,bytes([1]));pw(0x20005320,45875)
     if scenario=='bad_cookie':pw(0x2000ffe8,0)
     if scenario=='wrong_ticks':pw(0x20005320,42)
     if scenario=='inactive':put(0x2000536f,bytes([0]))
     if scenario=='coil':put(0x2000536c,bytes([1]))
     if scenario=='power_saving':put(0x20005370,bytes([1]))
     if scenario=='invalid_rtc':put(0x2000ffd8,bytes(6))
     if scenario=='old_rtc':put(0x2000ffd8,bytes([0,0,0,1,1,0]))
    pw(0xe000e200,0)
  elif op=='savebin':Path(parts[1]).write_bytes(get(int(parts[2],0),int(parts[3],0)))
  elif op in ['halt','sleep','exit','writedp','wait','connect','r']:pass
  else:raise RuntimeError('Unexpected JLink command '+line)
 print('J-Link mock completed')
sp.write_text(json.dumps(s))
