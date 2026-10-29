#!/usr/bin/env python3
# SEGGER J-Link Commander test double; never accesses real hardware.
import datetime,json,os,re,struct,sys
from pathlib import Path
args=sys.argv[1:];scenario=os.environ.get('MOCK_SCENARIO','')
sp=Path(os.environ['MOCK_STATE_PATH']);tp=Path(os.environ['MOCK_TRACE_PATH'])
SERVICE='VGL_Patrol_1.6_IDENTITY_RESET_SERVICE_FULL_S132_BOOTLOADER.hex'
APPROVED='VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex'
s=json.loads(sp.read_text())if sp.exists()else {'uicr':0x78000,'firmware':'old.hex','identity':[1]+list(b'1234567890ABCDEF123456'),'memory':{},'cpu_halted':False,'reset_started':False,'polls':0}
assert len(s['identity'])==23
mem=s['memory']
def event(x):
 with tp.open('a')as f:f.write(json.dumps(x)+'\n')
def put(a,b):
 for i,x in enumerate(b):mem[str(a+i)]=x
def get(a,n):return bytes(mem.get(str(a+i),0)for i in range(n))
def word(a):return int.from_bytes(get(a,4),'little')
def pw(a,v):put(a,struct.pack('<I',v))
def boot():
 s['cpu_halted']=False;put(0x2000536c,bytes(8));put(0x2000536f,bytes([s['identity'][0]]));put(0x20002a88,bytes(s['identity'][1:]));pw(0xe000e100,0x100000)
 if s['firmware']==SERVICE:
  put(0x2000ffc0,bytes(64));pw(0x2000fffc,0x31525342);put(0x2000536f,b'\0')
  if scenario=='missing_stamp':pw(0x2000fffc,0)
  if scenario=='disabled_irq':pw(0xe000e100,0)
  if scenario=='stale_request':pw(0x2000ffc0,123)
  if scenario=='busy_startup':pw(0x2000ffc8,1)
 if s['firmware']==APPROVED:
  if scenario=='ordinary_active':put(0x2000536f,b'\x01')
  if scenario=='ordinary_secret':put(0x20002a88,b'X')
  if scenario=='ordinary_coil':put(0x2000536c,b'\x01')
def completed():
 s['identity']=[0]+[0x30]*22
 if scenario=='flash_write':pw(0x2000ffc8,225);pw(0x2000ffcc,3);return
 if scenario=='flash_read':pw(0x2000ffc8,226);pw(0x2000ffd0,4);return
 if scenario=='mismatch':
  pw(0x2000ffc8,227);put(0x2000ffe0,b'\x01'+b'1234567890ABCDEF123456');return
 if scenario=='coil':pw(0x2000ffc8,228);return
 if scenario=='powersave':pw(0x2000ffc8,229);return
 pw(0x2000ffc8,2);pw(0x2000ffcc,0);pw(0x2000ffd0,0);pw(0x2000ffd4,23);pw(0x2000ffd8,0xa6f01602);put(0x2000ffe0,bytes(s['identity']))
 if scenario=='bad_ack':pw(0x2000ffd8,0)
 if scenario=='bad_length':pw(0x2000ffd4,22)
 if scenario=='bad_return':pw(0x2000ffd0,99)
 if scenario=='bad_request_consumed':pw(0x2000ffc0,0x31525346)
 if scenario=='changed_stamp':pw(0x2000fffc,0)
 if scenario=='bad_readback':put(0x2000ffe0,b'\x01')
 if scenario=='bad_readback_secret':put(0x2000ffe1,b'X')
if Path(sys.argv[0]).name.lower()=='nrfjprog.exe':
 event({'tool':'nrfjprog.exe','args':args,'path':sys.argv[0]})
 if '--ids'in args:
  print('123456789')
  if scenario=='multiple':print('987654321')
 elif '--recover'in args:
  if scenario=='recover_fail':print('ERROR: recovery failed');sys.exit(1)
  s['uicr']=0xffffffff;s['firmware']=None;mem.clear()
 elif '--memrd'in args:
  print('0x10001014: %08X'%(0x12345678 if scenario=='unexpected_uicr'else s['uicr']))
 elif '--memwr'in args:
  if scenario=='uicr_write_fail':print('ERROR: write failed');sys.exit(1)
  s['uicr']=int(args[args.index('--val')+1],0)
 elif '--program'in args:
  s['firmware']=Path(args[args.index('--program')+1]).name
  if scenario=='ordinary_program_fail'and s['firmware']==APPROVED:print('ERROR: program failed');sys.exit(1)
  print('Programming verified')
 elif '--reset'in args:
  if scenario=='service_replaced'and s['firmware']==SERVICE:s['firmware']='old.hex'
  if scenario=='ordinary_replaced'and s['firmware']==APPROVED:s['firmware']='old.hex'
  boot();print('Reset complete')
 elif '--verify'in args:
  if Path(args[args.index('--verify')+1]).name!=s['firmware']:print('ERROR: verify mismatch');sys.exit(1)
  print('Verified')
 else:raise RuntimeError('Unknown Nordic command '+repr(args))
else:
 event({'tool':'JLink.exe','args':args,'path':sys.argv[0]})
 assert '-CommandFile'in args
 file=Path(args[args.index('-CommandFile')+1]);lines=file.read_text().splitlines()
 print('SEGGER J-Link Commander V7.94e (Compiled Jan 15 2024 15:20:36)')
 print('J-Link uptime (since boot): N/A (Not supported by this model)')
 if scenario=='release_halted'and file.name=='Release_debug_power_FINAL.jlink':s['cpu_halted']=True
 if scenario.startswith('jlink_')and file.name=='Release_debug_power_FINAL.jlink':
  errors={'jlink_unknown':'Unknown command: WriteDP','jlink_unsupported':'Command WriteDP is not supported.','jlink_syntax':'Syntax: WriteDP <Index> <Value>','jlink_connect':'Cannot connect to target.'}
  print(errors[scenario]);sys.exit(0)
 for line in lines:
  event({'command':line});p=[x for x in re.split('[,\\s]+',line.strip())if x];op=p[0].lower()
  if op=='halt':s['cpu_halted']=True
  elif op=='go':
   if not s['cpu_halted']:print('****** Error: CPU is not halted')
   s['cpu_halted']=False
   if word(0x2000ffc0)==0x31525346 and word(0x2000ffc4)==0xa6f01602 and word(0xe000e200)&0x100000:
    pw(0x2000ffc0,0);pw(0xe000e200,0);s['reset_started']=True;s['polls']=0;pw(0x2000ffc8,1)
    if scenario=='not_processed':pw(0x2000ffc8,0)
    elif scenario not in('busy','busy_timeout'):completed()
   elif s['reset_started']and word(0x2000ffc8)==1 and scenario=='busy':
    s['polls']+=1
    if s['polls']>=2:completed()
  elif op=='savebin':
   a,n=int(p[2],16),int(p[3],16);Path(p[1]).write_bytes(get(a,n))
  elif op in('w4','w1'):
   a,v=int(p[1],0),int(p[2],0);put(a,v.to_bytes(4 if op=='w4'else 1,'little'))
  elif op in('sleep','writedp','exit'):pass
  else:raise RuntimeError('Unknown JLink command '+line)
 print('Script processing completed.')
sp.write_text(json.dumps(s))
