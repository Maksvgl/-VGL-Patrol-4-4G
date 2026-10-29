#!/usr/bin/env python3
"""Execute reset PowerShell driver with fake tools, never hardware."""
import argparse,datetime,hashlib,json,os,subprocess,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parent
DRIVER=ROOT.parent/'windows'/'Reset_Identity.ps1'
DRIVER_BYTES=DRIVER.read_bytes()
SERVICE='VGL_Patrol_1.6_IDENTITY_RESET_SERVICE_FULL_S132_BOOTLOADER.hex'
APPROVED='VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex'
BASE=next((a/b/APPROVED for a in (ROOT,*ROOT.parents) for b in [Path('firmware/releases/v1.6-1400ms-rf5ms'),Path('patrol_current_release/firmware/releases/v1.6-1400ms-rf5ms')] if(a/b/APPROVED).is_file()),ROOT/'missing-approved-baseline.hex')
CASE_ROOT=ROOT/'cases'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S')+'_'+uuid.uuid4().hex)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(pwsh,index,name,scenario='',expected=0,needle='FACTORY_RESET_COMPLETE.',args=None,discovery='',corrupt=False,traversal=False,approved_wrong=False):
 folder=CASE_ROOT/f'{index:02d} {name} with spaces';folder.mkdir(parents=True)
 (folder/DRIVER.name).write_bytes(DRIVER_BYTES);(folder/SERVICE).write_text(':00000001FF\n');(folder/APPROVED).write_bytes(BASE.read_bytes()if not approved_wrong else b':00000001FF\n')
 manifest={'firmware_file':SERVICE,'files':[{'file':p.name,'sha256':sha(p)}for p in[folder/DRIVER.name,folder/SERVICE,folder/APPROVED]]}
 if traversal:manifest['files'][0]['file']='../Reset_Identity.ps1'
 (folder/'flash_manifest.json').write_text(json.dumps(manifest))
 if corrupt:(folder/SERVICE).write_text('bad')
 trace=folder/'trace.jsonl';state=folder/'state.json';env=os.environ.copy();args=list(args or[])
 paths=[str(CASE_ROOT/'bin')]
 if discovery:
  mock=ROOT/'mock_tool.py';nrfbin=folder/'Nordic bin';javabin=folder/'Java bin';seggerbin=folder/'SEGGER bin';programfiles=folder/'Program Files';installed=programfiles/'SEGGER'/'JLink_V794e'
  for p in[nrfbin,javabin,seggerbin,installed]:p.mkdir(parents=True)
  (nrfbin/'nrfjprog.exe').symlink_to(mock);(seggerbin/'JLink.exe').symlink_to(mock);(installed/'JLink.exe').symlink_to(mock)
  java=javabin/'JLink.exe';java.write_text("#!/usr/bin/env python3\nimport sys\nprint('Usage: jlink --module-path <path> --add-modules <module>')\nsys.exit(2)\n");java.chmod(0o755)
  if discovery in['java_only','missing','path_second']:(installed/'JLink.exe').unlink()
  paths=[str(nrfbin)]
  if discovery!='missing':paths.append(str(javabin))
  if discovery=='path_second':paths.append(str(seggerbin))
  env.update({'ProgramFiles':str(programfiles),'ProgramFiles(x86)':str(folder/'Empty x86'),'ProgramW6432':str(programfiles)})
  args=[a.replace('{JAVA}',str(java)).replace('{SEGGER}',str(installed/'JLink.exe')).replace('{MISSING}',str(folder/'Missing.exe'))for a in args]
 env.update({'PATH':os.pathsep.join(paths+[env['PATH']]),'MOCK_STATE_PATH':str(state),'MOCK_TRACE_PATH':str(trace),'MOCK_SCENARIO':scenario})
 p=subprocess.run([str(pwsh),'-NoProfile','-NonInteractive','-File',str(folder/DRIVER.name),*args],env=env,capture_output=True,text=True,timeout=100)
 output=p.stdout+p.stderr;(folder/'output.txt').write_text(output)
 events=[json.loads(x)for x in trace.read_text().splitlines()]if trace.exists()else[]
 calls=[x for x in events if 'tool'in x];commands=[x['command']for x in events if 'command'in x];issues=[]
 if p.returncode!=expected:issues.append(f'exit {p.returncode} expected{expected}')
 if needle and needle not in output:issues.append('missing '+needle)
 no_hardware=corrupt or traversal or approved_wrong or '-ValidateOnly'in args or(discovery in['java_only','missing']or'{JAVA}'in(args or[]))
 if no_hardware and calls:issues.append('tool access before package/tool validation')
 if discovery and expected!=0 and calls:issues.append('discoveryfailureaccessedhardware')
 programs=[x for x in calls if '--program'in x['args']];recovers=[x for x in calls if '--recover'in x['args']]
 if expected==0 and p.returncode==0 and '-ValidateOnly'not in args:
  if len(recovers)!=1:issues.append('exactly one explicit recovery required')
  if len(programs)!=2 or[Path(x['args'][x['args'].index('--program')+1]).name for x in programs]!=[SERVICE,APPROVED]:issues.append('service then exactordinary program sequence missing')
  if commands[-5:]!=['Halt','Go','WriteDP 2, 0','WriteDP 1, 0','Exit']:issues.append('finaldebugreleasewrong')
  if events[-1]!={'command':'Exit'}:issues.append('targetaccessafterrelease')
  report=list(folder.glob('logs/*/result.json'))
  if len(report)!=1 or not json.loads(report[0].read_text()).get('success'):issues.append('missingfinalsuccessreport')
  if state.exists():
   s=json.loads(state.read_text())
   if s['identity']!=[0]+[0x30]*22:issues.append('persistentidentitynotcleared')
   if s['firmware']!=APPROVED:issues.append('serviceimageleftinstalled')
   if s.get('cpu_halted'):issues.append('finalCPUleftstopped')
  # Require two standalone post-boot verifies, one for each image.
  verified=[Path(x['args'][x['args'].index('--verify')+1]).name for x in calls if '--verify'in x['args'] and '--program'not in x['args']and '--memwr'not in x['args']]
  if verified!=[SERVICE,APPROVED]:issues.append('postbootverificationorderwrong')
 if expected!=0 and list(folder.glob('logs/*/result.json')):issues.append('failurewrotesuccessreport')
 if scenario in['flash_write','flash_read','mismatch','coil','powersave','bad_ack','bad_length','bad_return','bad_request_consumed','changed_stamp','busy_timeout','not_processed']:
  if any(APPROVED in x['args']for x in programs):issues.append('ordinaryrestorebeforeclearverification')
  if any('cleared_identity.bin'in c for c in commands):issues.append('failedresetsavedsecretreadbackscratch')
 for c in commands:
  if c.lower().startswith('savebin '):
   f=c.replace(',',' ').split();a,n=int(f[2],16),int(f[3],16)
   expected_n=32 if f[1].startswith('reset_header')else(23 if f[1]=='cleared_identity.bin'else(22 if f[1]=='ordinary_identity.bin'else(8 if f[1]=='ordinary_flags.bin'else 4)))
   if n!=expected_n:issues.append('wronghexSaveBinsize '+f[1])
   if 0x2000ffc0<=a<0x20010000 and a+n>0x20010000:issues.append('RAMdumpoverrun')
  if c.lower().startswith('w1 '):issues.append('hostunexpectedbytewriteRTC/identity')
 # The only host RAM writes are one-time cookie/magic and the known IRQ pending.
 writes=[c for c in commands if c.lower().startswith('w4 ')]
 if writes and writes!=['W4 0x2000FFC4, 0xA6F01602','W4 0x2000FFC0, 0x31525346','W4 0xE000E200, 0x00100000']:issues.append('unexpectedtargetRAMwrites')
 if scenario in['missing_stamp','disabled_irq','stale_request','busy_startup','service_replaced']and writes:issues.append('requestsentbeforestartupvalid')
 return {'name':name,'passed':not issues,'exit_code':p.returncode,'expected_exit_code':expected,'issues':issues,'tool_calls':len(calls),'jlink_commands':len(commands)}
def main():
 p=argparse.ArgumentParser();p.add_argument('--pwsh',default=str(ROOT.parents[1]/'patrol_factory_ppk'/'host_tests'/'runtime'/'pwsh'));o=p.parse_args();pwsh=Path(o.pwsh).resolve()
 assert pwsh.is_file()and BASE.is_file()
 bindir=CASE_ROOT/'bin';bindir.mkdir(parents=True);mock=ROOT/'mock_tool.py';mock.chmod(0o755)
 for n in['nrfjprog.exe','JLink.exe']:(bindir/n).symlink_to(mock)
 cases=[
 ('validate_only',dict(args=['-ValidateOnly'],needle='PACKAGE CHECK PASSED.')),
 ('reset_old_identity',{}),('release_initially_halted',dict(scenario='release_halted')),
 ('bounded_busy_then_complete',dict(scenario='busy')),
 ('explicit_probe',dict(scenario='multiple',args=['-ProbeSerial','987654321'])),
 ('multiple_probes',dict(scenario='multiple',expected=1,needle='Connect exactly one SWD probe')),
 ('invalid_probe',dict(args=['-ProbeSerial','bad'],expected=1,needle='ProbeSerial must contain')),
 ('recover_failure',dict(scenario='recover_fail',expected=1,needle='Recover_processor failed')),
 ('unexpected_uicr',dict(scenario='unexpected_uicr',expected=1,needle='Unexpected UICR')),
 ('uicr_write_failure',dict(scenario='uicr_write_fail',expected=1,needle='Restore_verified_bootloader_address failed')),
 ('service_replaced_after_boot',dict(scenario='service_replaced',expected=1,needle='Verify_service_after_boot failed')),
 ('missing_stamp',dict(scenario='missing_stamp',expected=1,needle='startup stamp is absent')),
 ('disabled_irq',dict(scenario='disabled_irq',expected=1,needle='IRQ20 is disabled')),
 ('stale_request',dict(scenario='stale_request',expected=1,needle='fresh idle state')),
 ('busy_startup',dict(scenario='busy_startup',expected=1,needle='fresh idle state')),
 ]
 for scen in['flash_write','flash_read','mismatch','coil','powersave','busy_timeout','not_processed']:
  cases.append((scen,dict(scenario=scen,expected=1,needle='Identity reset failed')))
 for scen,needle in[('bad_ack','acknowledgement is invalid'),('bad_length','verify all 23'),('bad_return','did not return success'),('bad_request_consumed','not consumed'),('changed_stamp','stamp changed'),('bad_readback','activation flag was not cleared'),('bad_readback_secret','secret or number was not reset')]:
  cases.append((scen,dict(scenario=scen,expected=1,needle=needle)))
 for scen,needle in[('ordinary_program_fail','Program_ordinary_approved_1_6 failed'),('ordinary_replaced','Verify_ordinary_firmware_after_boot failed'),('ordinary_active','did not start inactive'),('ordinary_secret','did not load the unconfigured'),('ordinary_coil','reports coil power')]:
  cases.append((scen,dict(scenario=scen,expected=1,needle=needle)))
 for scen in['jlink_unknown','jlink_unsupported','jlink_syntax','jlink_connect']:cases.append((scen,dict(scenario=scen,expected=1,needle='J-Link reported a failure')))
 cases.extend([
 ('bad_checksum',dict(corrupt=True,expected=1,needle='SHA-256 mismatch')),
 ('unsafe_manifest',dict(traversal=True,expected=1,needle='Unsafe filename')),
 ('wrong_ordinary_image',dict(approved_wrong=True,expected=1,needle='not the unchanged owner-approved')),
 ('java_first_segger_installed',dict(discovery='installed')),
 ('java_first_segger_second_path',dict(discovery='path_second')),
 ('java_only_blocked_before_recover',dict(discovery='java_only',expected=1,needle='SEGGER')),
 ('missing_segger_blocked_before_recover',dict(discovery='missing',expected=1,needle='SEGGER')),
 ('explicit_java_blocked',dict(discovery='installed',args=['-JLinkPath','{JAVA}'],expected=1,needle='not SEGGER')),
 ('explicit_segger',dict(discovery='installed',args=['-JLinkPath','{SEGGER}'])),
 ('explicit_missing',dict(discovery='installed',args=['-JLinkPath','{MISSING}'],expected=1,needle='explicit JLinkPath does not exist')),
 ])
 results=[run(pwsh,i,name,**opts)for i,(name,opts)in enumerate(cases,1)]
 version=subprocess.run([str(pwsh),'-NoProfile','-Command','$PSVersionTable.PSVersion.ToString()'],capture_output=True,text=True,check=True).stdout.strip()
 summary={'driver_sha256':hashlib.sha256(DRIVER_BYTES).hexdigest(),'runtime':'PowerShell '+version+' Linux','hardware_access':False,'scope':'Actual PowerShell host logic with fake Nordic/SEGGER executables; no Windows or electrical proof','passed':sum(r['passed']for r in results),'total':len(results),'results':results}
 (ROOT/'results.json').write_text(json.dumps(summary,indent=2)+'\n')
 for r in results:print(('PASS 'if r['passed']else'FAIL ')+r['name']+(': '+'; '.join(r['issues'])if r['issues']else''))
 print(f"{summary['passed']}/{summary['total']} passed; driver SHA256 {summary['driver_sha256']}")
 return 0 if summary['passed']==summary['total']else 1
if __name__=='__main__':raise SystemExit(main())
