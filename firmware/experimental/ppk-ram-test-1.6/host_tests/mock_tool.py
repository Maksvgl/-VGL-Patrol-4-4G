#!/usr/bin/env python3
import json,os,sys,re,struct,datetime
from pathlib import Path
args=sys.argv[1:]
sp=Path(os.environ['MOCK_STATE_PATH']);tp=Path(os.environ['MOCK_TRACE_PATH']);scenario=os.environ.get('MOCK_SCENARIO','')
# Discovery may inspect executables, but must not open a probe or write target state.
if Path(sys.argv[0]).name.lower()=='jlink.exe':
 is_java=Path(sys.argv[0]).parent.name.lower()=='java bin'
 if is_java or '-CommandFile' not in args:
  with tp.open('a')as f:f.write(json.dumps({'identity': 'java' if is_java else 'segger','path':sys.argv[0],'args':args})+'\n')
  if is_java:
   print('Error: unknown option: '+(args[0]if args else ''))
   print('Usage: jlink <options> --module-path <modulepath> --add-modules <module>[,<module>...]')
   sys.exit(2)
  print('SEGGER J-Link Commander V9.20 (Compiled Oct 22 2026)')
  print('DLL version V9.20, compiled Oct 22 2026')
  print('J-Link Commander command line options: -Device -If -USB -CommandFile')
  sys.exit(0)
s=json.loads(sp.read_text())if sp.exists()else {'uicr':0x78000,'memory':{},'firmware':'VGL_Patrol_1.6_PPK_RAM_TEST_FULL_S132_BOOTLOADER.hex','cpu_halted':False}
s.setdefault('cpu_halted',False)
mem=s['memory']
def put(addr,data):
 for i,b in enumerate(data):mem[str(addr+i)]=b
def get(addr,n):return bytes(mem.get(str(addr+i),0)for i in range(n))
def word(addr):return int.from_bytes(get(addr,4),'little')
def pw(addr,v):put(addr,struct.pack('<I',v))
def boot():
 s['cpu_halted']=False
 put(0x2000ffc0,bytes(64));pw(0x2000fffc,0x31545046);put(0x2000536c,bytes(8));put(0x2000536f,bytes([1 if scenario=='normal_active'else 0]));pw(0x20005320,45875);pw(0xe000e100,0x100000)
 if scenario=='irq_disabled':pw(0xe000e100,0)
 if scenario=='missing_stamp':pw(0x2000fffc,0)
 if scenario=='stale_mailbox':pw(0x2000ffcc,1)
 if scenario=='running_request':pw(0x2000ffc8,1)
if not mem:boot()
with tp.open('a')as f:f.write(json.dumps({'tool':Path(sys.argv[0]).name,'path':sys.argv[0],'args':args})+'\n')
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
 # J-Link sessions preserve the target's CPU state. A running target is not
 # implicitly halted by opening the probe; Go on it produces a zero-exit error.
 if scenario=='release_initially_halted' and 'Release_debug_power_FINAL' in script.name:s['cpu_halted']=True
 if scenario=='cleanup_halted' and 'Release_debug_power_after_failure' in script.name:s['cpu_halted']=True
 if scenario=='jlink_error'and 'Release_debug_power_FINAL'in script.name:print('Unknown command: WriteDP');sys.exit(0)
 if scenario=='jlink_benign_uptime':
  print('SEGGER J-Link Commander V7.94e (Compiled Jan 15 2024 15:20:36)')
  print('J-Link uptime (since boot): N/A (Not supported by this model)')
  print('Connecting to target via SWD')
  print('Cortex-M4 identified.')
 real_errors={
  'jlink_unsupported_command':'Command WriteDP is not supported.',
  'jlink_unsupported_command_prefix':'Command not supported: WriteDP',
  'jlink_syntax_error':'Syntax: WriteDP <RegIndex> <Data>',
  'jlink_connect_failure':'Cannot connect to target.',
  'jlink_error_line':'ERROR: Failed to read memory',
 }
 if scenario in real_errors and 'Release_debug_power_FINAL'in script.name:
  print(real_errors[scenario]);sys.exit(0)
 for line in lines:
  with tp.open('a')as f:f.write(json.dumps({'command':line})+'\n')
  parts=re.split('[,\\s]+',line.strip());parts=[x for x in parts if x]
  op=parts[0].lower()
  if op in ['w4','w1']:
   a=int(parts[1],0);v=int(parts[2],0);put(a,v.to_bytes(4 if op=='w4'else 1,'little'))
  elif op=='go':
   if not s['cpu_halted']:
    print('****** Error: CPU is not halted')
    continue
   s['cpu_halted']=False
   if word(0x2000ffc0)==0x31544b50 and word(0x2000ffc4)==0xa6f01601 and word(0xe000e200)&0x100000:
    rtc_failure=scenario in ('rtc_failure','cleanup_running','cleanup_halted')
    pw(0x2000ffc0,0);pw(0x2000ffcc,0);pw(0x2000ffd0,2 if rtc_failure else 0)
    pw(0x2000ffc8,226 if rtc_failure else 2)
    if not rtc_failure:
     put(0x2000ffd8,get(0x2000ffe0,6));pw(0x2000ffd4,45875);pw(0x2000ffe8,0xa6f01601);put(0x2000536f,bytes([1]));pw(0x20005320,45875)
     if scenario=='bad_cookie':pw(0x2000ffe8,0)
     if scenario=='wrong_ticks':pw(0x20005320,42)
     if scenario=='inactive':put(0x2000536f,bytes([0]))
     if scenario=='coil':put(0x2000536c,bytes([1]))
     if scenario=='power_saving':put(0x20005370,bytes([1]))
     if scenario=='invalid_rtc':put(0x2000ffd8,bytes(6))
     if scenario=='old_rtc':put(0x2000ffd8,bytes([0,0,0,1,1,0]))
    pw(0xe000e200,0)
  elif op=='savebin':
   # J-Link Commander interprets address and NumBytes as hexadecimal, including
   # bare digits: `64` requests 0x64=100 bytes, while `0x40` requests 64.
   address=int(parts[2],16);count=int(parts[3],16)
   Path(parts[1]).write_bytes(get(address,count))
   print('Reading %d bytes from addr 0x%08X into file...O.K.'%(count,address))
  elif op=='halt':s['cpu_halted']=True
  elif op in ['sleep','exit','writedp','wait','connect','r']:pass
  else:raise RuntimeError('Unexpected JLink command '+line)
 print('J-Link mock completed')
sp.write_text(json.dumps(s))
