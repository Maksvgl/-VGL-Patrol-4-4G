# Windows PowerShell 5.1. Host commands do not erase/program external W25Q or EEPROM.
# The firmware's unchanged normal first-boot initialization can still initialize them.
[CmdletBinding()]
param(
    [switch]$Flash,
    [switch]$Recover,
    [string]$ProbeSerial = '',
    [string]$JLinkPath = '',
    [switch]$ValidateOnly,
    [switch]$CheckNormal,
    [switch]$RestoreApproved
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:Root = $PSScriptRoot
$script:OutDir = $null
$script:Probe = $null
$script:Nrf = $null
$script:JLink = $null
$script:JLinkTouched = $false
$script:Released = $false
$script:Step = 0
$Mailbox = [uint32]0x2000FFC0
$BootStamp = [uint32]0x31545046
$RequestMagic = [uint32]0x31544B50
$Cookie = [Convert]::ToUInt32('A6F01601', 16)
$ExpectedTicks = [uint32]45875
$FirmwareName = 'VGL_Patrol_1.6_PPK_RAM_TEST_FULL_S132_BOOTLOADER.hex'
$ApprovedFirmwareName = 'VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Quote-NativeArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $Value = $Value -replace '(\\*)"', '$1$1\"'
    $Value = $Value -replace '(\\+)$', '$1$1'
    return '"' + $Value + '"'
}

function Invoke-Tool([string]$Exe, [string[]]$Arguments, [string]$Label, [int]$TimeoutSeconds = 180) {
    $script:Step++
    Write-Host ('[{0}] {1}' -f $script:Step, $Label)
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Exe
    $info.Arguments = (($Arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' ')
    $info.WorkingDirectory = $script:Root
    if ($script:OutDir) { $info.WorkingDirectory = $script:OutDir }
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    Assert-True ($process.Start()) ('Cannot start ' + $Exe)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) {
        try { $process.Kill() } catch { }
        $process.WaitForExit()
    }
    $output = $stdoutTask.Result + [Environment]::NewLine + $stderrTask.Result
    $exitCode = $process.ExitCode
    $process.Dispose()
    if ($script:OutDir) {
        $logName = '{0:D2}-{1}.log' -f $script:Step, ($Label -replace '[^A-Za-z0-9_-]', '_')
        $log = 'EXE: ' + $Exe + [Environment]::NewLine + 'ARGS: ' + $info.Arguments + [Environment]::NewLine + $output
        [IO.File]::WriteAllText((Join-Path $script:OutDir $logName), $log, (New-Object Text.UTF8Encoding($false)))
    }
    if ($timedOut) { throw ($Label + ' timed out. Stop this test and inspect the board/probe.') }
    if ($exitCode -ne 0) { throw ($Label + ' failed, exit ' + $exitCode + '. ' + $output.Trim()) }
    return $output
}

function Invoke-Nrf([string[]]$Arguments, [string]$Label) {
    return Invoke-Tool $script:Nrf (@('-f', 'NRF52', '--snr', $script:Probe) + $Arguments) $Label
}

function Invoke-JLink([string]$Name, [string[]]$Commands) {
    $script:JLinkTouched = $true
    $commandFile = $Name + '.jlink'
    # Commands contain only ASCII filenames. J-Link runs inside this run's log directory.
    [IO.File]::WriteAllLines((Join-Path $script:OutDir $commandFile), $Commands, [Text.Encoding]::ASCII)
    $output = Invoke-Tool $script:JLink @('-Device', 'NRF52832_XXAA', '-If', 'SWD', '-Speed', '1000', '-USB', $script:Probe, '-AutoConnect', '1', '-NoGui', '1', '-ExitOnError', '1', '-CommandFile', $commandFile) $Name 60
    # Probe capability information such as "J-Link uptime ... Not supported by
    # this model" is not a failed command. Still reject unsupported operations.
    if ($output -match '(?im)(ERROR:|J-Link\s+ERROR|Failed to (connect|read|write|download|open)|Cannot (connect|read|write|open)|Could not (connect|read|write|open)|No emulators connected|Unknown command|Invalid command|Unsupported command|Syntax:|\b(?:command|operation|function|feature|WriteDP|SaveBin|SWD|read|write)\b[^\r\n]*\bnot supported\b)') {
        throw ('J-Link reported a failure in ' + $Name + '. ' + $output.Trim())
    }
    return $output
}

function Read-Binary([string]$Name, [int]$Length) {
    $path = Join-Path $script:OutDir $Name
    Assert-True (Test-Path -LiteralPath $path -PathType Leaf) ('J-Link did not create ' + $Name)
    $bytes = [IO.File]::ReadAllBytes($path)
    Assert-True ($bytes.Length -eq $Length) ($Name + ' has an unexpected length.')
    return ,$bytes
}

function U32([byte[]]$Bytes, [int]$Offset) { return [BitConverter]::ToUInt32($Bytes, $Offset) }

function Read-UicrBootAddress {
    $output = Invoke-Nrf @('--memrd', '0x10001014', '--w', '32', '--n', '4') 'Read_bootloader_UICR'
    $match = [regex]::Match($output, '(?im)^\s*0x10001014\s*:\s*([0-9a-f]{8})\b')
    Assert-True $match.Success 'Cannot parse the UICR bootloader-address readback.'
    return [Convert]::ToUInt32($match.Groups[1].Value, 16)
}

function Find-Application([string]$Name) {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    return $null
}

function Test-SeggerJLink([string]$Path) {
    # Java's jlink.exe has the same filename. Identify the executable before
    # any nrfjprog/target access; never run an unverified candidate to detect it.
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try {
        $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
        if ($version.CompanyName) {
            return (($version.CompanyName -match 'SEGGER') -and
                (($version.FileDescription -match 'J[- ]?Link.*Commander') -or
                 ($version.OriginalFilename -ieq 'JLink.exe')))
        }
    } catch { }
    # Some distributions have no version resource. Require the exact embedded
    # Commander banner (ASCII or UTF-16), not the filename or installation path.
    try {
        $item = Get-Item -LiteralPath $Path
        if ($item.Length -gt 16777216) { return $false }
        $bytes = [IO.File]::ReadAllBytes($Path)
        return (([Text.Encoding]::ASCII.GetString($bytes).Contains('SEGGER J-Link Commander')) -or
            ([Text.Encoding]::Unicode.GetString($bytes).Contains('SEGGER J-Link Commander')))
    } catch { return $false }
}

function Find-JLink([string]$ExplicitPath) {
    if ($ExplicitPath) {
        Assert-True (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) 'The explicit JLinkPath does not exist.'
        $resolved = (Resolve-Path -LiteralPath $ExplicitPath).Path
        Assert-True (Test-SeggerJLink $resolved) 'The explicit JLinkPath is not SEGGER J-Link Commander. Java jlink.exe cannot be used.'
        return $resolved
    }
    $candidates = @()
    foreach ($base in @($env:ProgramW6432, $env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $base) { continue }
        $segger = Join-Path $base 'SEGGER'
        if (-not (Test-Path -LiteralPath $segger -PathType Container)) { continue }
        $candidates += @(Get-ChildItem -LiteralPath $segger -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'JLink.exe' } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    }
    # Search all PATH matches too: a Java installation can precede SEGGER.
    $candidates += @(Get-Command 'JLink.exe' -All -CommandType Application -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Source })
    foreach ($candidate in @($candidates | Select-Object -Unique)) {
        if (Test-SeggerJLink $candidate) { return $candidate }
        Write-Host ('Skipping non-SEGGER JLink.exe: ' + $candidate)
    }
    throw 'SEGGER J-Link Commander was not found. Java jlink.exe cannot be used. Install SEGGER J-Link 7.92d or newer, or pass -JLinkPath with its full path. No processor operation was started.'
}

function Validate-Package {
    $manifestPath = Join-Path $script:Root 'flash_manifest.json'
    Assert-True (Test-Path -LiteralPath $manifestPath -PathType Leaf) 'flash_manifest.json is missing. Extract the complete package.'
    $manifest = [IO.File]::ReadAllText($manifestPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
    Assert-True ($manifest.firmware_file -ceq $FirmwareName) 'This manifest belongs to a different firmware package.'
    Assert-True (@($manifest.files).Count -gt 0) 'The manifest has no files.'
    $foundFirmware = $false
    $foundApprovedFirmware = $false
    foreach ($entry in $manifest.files) {
        $name = [string]$entry.file
        Assert-True (($name -ne '') -and ([IO.Path]::GetFileName($name) -ceq $name) -and ($name -notmatch '[\\/:]')) 'Unsafe filename in manifest.'
        Assert-True ([string]$entry.sha256 -match '^[0-9a-fA-F]{64}$') ('Invalid SHA-256 for ' + $name)
        $path = Join-Path $script:Root $name
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) ('Package file is missing: ' + $name)
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        Assert-True ($actual -ieq [string]$entry.sha256) ('SHA-256 mismatch: ' + $name + '. Do not use this package.')
        if ($name -ceq $FirmwareName) { $foundFirmware = $true }
        if ($name -ceq $ApprovedFirmwareName) { $foundApprovedFirmware = $true }
    }
    Assert-True $foundFirmware 'The firmware HEX is not included in the manifest checksums.'
    if ($RestoreApproved) {
        Assert-True $foundApprovedFirmware 'The unchanged approved 1.6 HEX is missing from the manifest checksums.'
    }
}

function Release-DebugPower([string]$Name) {
    # This MUST be the final target access. Do not reconnect, reset or read after it.
    # Earlier sessions resume the CPU. Commander 7.94e rejects Go on an already
    # running core, so establish Halt before Go without resetting RAM or RTC.
    $null = Invoke-JLink $Name @('Halt', 'Go', 'WriteDP 2, 0', 'WriteDP 1, 0', 'Exit')
    $script:Released = $true
}

try {
    Assert-True ((-not $Recover) -or $Flash) '-Recover is valid only with -Flash.'
    Assert-True ((-not $RestoreApproved) -or $Flash) '-RestoreApproved requires -Flash.'
    Assert-True ((-not $CheckNormal) -or ((-not $Flash) -and (-not $Recover) -and (-not $RestoreApproved))) '-CheckNormal cannot be combined with Flash, Recover or RestoreApproved.'
    Assert-True ((-not $ValidateOnly) -or ((-not $Flash) -and (-not $Recover) -and (-not $CheckNormal) -and (-not $RestoreApproved))) '-ValidateOnly cannot be combined with a hardware operation.'
    Validate-Package
    if ($ValidateOnly) { Write-Host 'PACKAGE CHECK PASSED. No hardware was accessed.'; exit 0 }
    $selectedFirmwareName = $FirmwareName
    if ($RestoreApproved) { $selectedFirmwareName = $ApprovedFirmwareName }

    $script:Nrf = Find-Application 'nrfjprog.exe'
    Assert-True ([bool]$script:Nrf) 'nrfjprog.exe was not found in PATH. Install Nordic nRF Command Line Tools.'
    $script:JLink = Find-JLink $JLinkPath
    Write-Host ('SEGGER J-Link Commander: ' + $script:JLink)
    $idsOutput = Invoke-Tool $script:Nrf @('--ids') 'List_probes'
    $ids = @($idsOutput -split '\r?\n' | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '^\d{6,12}$' } | Select-Object -Unique)
    if ($ProbeSerial) {
        Assert-True ($ProbeSerial -match '^\d{6,12}$') 'ProbeSerial must contain 6 to 12 digits.'
        Assert-True ($ids -contains $ProbeSerial) 'The selected probe was not returned by nrfjprog --ids.'
        $script:Probe = $ProbeSerial
    } else {
        Assert-True ($ids.Count -eq 1) 'Connect exactly one SWD probe, or pass its serial number as the BAT first argument.'
        $script:Probe = $ids[0]
    }
    $runName = (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '_probe_' + $script:Probe + '_' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
    $script:OutDir = Join-Path (Join-Path $script:Root 'logs') $runName
    $null = New-Item -ItemType Directory -Path $script:OutDir -Force
    Write-Host ('Selected SWD probe: ' + $script:Probe)
    Write-Host ('Logs: ' + $script:OutDir)

    if ($Flash) {
        if ($Recover) {
            Write-Host 'RECOVER: erasing the processor user Flash, UICR and RAM. External W25Q/EEPROM are not erased.' -ForegroundColor Yellow
            $null = Invoke-Nrf @('--recover') 'Recover_processor'
        }
        try { $oldUicr = Read-UicrBootAddress } catch {
            throw ('Cannot access the processor configuration. If it is read-protected, use RECOVER_AND_START_PPK_TEST.bat. ' + $_.Exception.Message)
        }
        Assert-True (($oldUicr -eq [uint32]0x00078000) -or ($oldUicr -eq [uint32]::MaxValue)) ('Unexpected UICR bootloader address 0x{0:X8}. Flashing was not started; do not erase this configuration automatically.' -f $oldUicr)
        $null = Invoke-Nrf @('--program', (Join-Path $script:Root $selectedFirmwareName), '--sectorerase', '--verify') 'Program_and_verify_selected_firmware'
        if ($oldUicr -eq [uint32]::MaxValue) {
            $null = Invoke-Nrf @('--memwr', '0x10001014', '--val', '0x00078000', '--verify') 'Restore_verified_bootloader_address'
        }
        $newUicr = Read-UicrBootAddress
        Assert-True ($newUicr -eq [uint32]0x00078000) 'UICR bootloader-address readback failed.'
        $null = Invoke-Nrf @('--reset') 'Soft_reset_and_start'
        Start-Sleep -Milliseconds 2000
    } else {
        # Verify the installed image without changing Flash or resetting the target.
        try {
            $null = Invoke-Nrf @('--verify', (Join-Path $script:Root $FirmwareName)) 'Verify_installed_PPK_firmware'
        } catch {
            if ($CheckNormal) {
                throw ('Normal boot cannot be inspected through SWD. This check does not unlock, erase or reset the processor; do not run recover merely to make this normal-state check pass. ' + $_.Exception.Message)
            }
            throw
        }
    }

    if ($RestoreApproved) {
        # The custom bootloader can consume a pre-existing external pending update.
        # Verify again after startup, before closing debug access, to confirm the
        # approved application was not replaced during that boot.
        $null = Invoke-Nrf @('--verify', (Join-Path $script:Root $ApprovedFirmwareName)) 'Verify_approved_firmware_after_boot'
        Release-DebugPower 'Release_debug_power_FINAL'
        # Only host reporting follows; the approved image has no factory mailbox.
        $restoreReport = [ordered]@{
            success = $true; operation = 'restore_approved'; probe_serial = $script:Probe;
            completed_local = (Get-Date).ToString('o'); firmware = $selectedFirmwareName;
            flash_performed = $true; recover_performed = [bool]$Recover;
            factory_request_sent = $false; debug_power_release_commands_completed = $true;
            external_settings_note = 'Existing external activation, serial and secret were not erased by host commands.'
        }
        [IO.File]::WriteAllText((Join-Path $script:OutDir 'result.json'), ($restoreReport | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
        Write-Host ''
        Write-Host 'APPROVED_1_6_RESTORED.' -ForegroundColor Green
        Write-Host 'The unchanged approved 1.6 HEX was programmed and verified. No factory request was sent.'
        exit 0
    }

    # SaveBin interprets its length as hexadecimal: decimal 64 must be 0x40.
    $null = Invoke-JLink 'Read_startup_mailbox' @('Halt', 'SaveBin mailbox_before.bin, 0x2000FFC0, 0x40', 'SaveBin flags_before.bin, 0x2000536C, 0x08', 'SaveBin nvic_iser0.bin, 0xE000E100, 0x04', 'Go', 'Exit')
    $before = Read-Binary 'mailbox_before.bin' 64
    Assert-True ((U32 $before 60) -eq $BootStamp) 'The factory startup stamp is absent. The image did not reach normal startup; inspect LED error codes and do not trust a low current reading.'
    $iser = Read-Binary 'nvic_iser0.bin' 4
    Assert-True (((U32 $iser 0) -band [uint32]0x00100000) -ne 0) 'The existing app_timer SWI0 interrupt (IRQ20) is disabled. The driver will not enable or pend a disabled interrupt; normal startup must complete successfully.'

    if ($CheckNormal) {
        $normalFlags = Read-Binary 'flags_before.bin' 8
        Assert-True ((U32 $before 8) -eq 0) 'The mailbox is not in normal-boot status 0. Remove and reapply board power before this check; this script will not reset it for you.'
        Assert-True ((U32 $before 0) -eq 0) 'A factory request is pending. Normal boot was not confirmed.'
        Assert-True ((U32 $before 40) -eq 0) 'The factory acknowledgement was not cleared by startup.'
        Assert-True (@($before[0..59] | Where-Object { $_ -ne 0 }).Count -eq 0) 'The startup mailbox payload is not fully clear. A previous or interrupted preparation is not proof of a normal reboot.'
        Assert-True (($normalFlags[3] -eq 0) -or ($normalFlags[3] -eq 1)) 'The normal activation flag is invalid.'
        Release-DebugPower 'Release_debug_power_FINAL'
        # Only host reporting follows. No RTC, Flash, UICR or RAM writes in this path.
        $normalReport = [ordered]@{
            success = $true; operation = 'check_normal_boot'; probe_serial = $script:Probe;
            completed_local = (Get-Date).ToString('o'); firmware = $FirmwareName;
            flash_performed = $false; recover_performed = $false; reset_performed = $false;
            factory_request_sent = $false; mailbox_status = (U32 $before 8);
            mailbox_request = (U32 $before 0); mailbox_acknowledgement = (U32 $before 40);
            active_ram = [int]$normalFlags[3]; coil_ram = [int]$normalFlags[0]; power_saving_ram = [int]$normalFlags[4];
            debug_power_release_commands_completed = $true;
            activation_note = 'Normal boot restores existing external settings; active=0 and active=1 are both permitted.'
        }
        [IO.File]::WriteAllText((Join-Path $script:OutDir 'result.json'), ($normalReport | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
        Write-Host ''
        Write-Host 'NORMAL_BOOT_CONFIRMED.' -ForegroundColor Green
        Write-Host ('Factory mailbox is clear. Existing normal activation flag: ' + $normalFlags[3])
        Write-Host 'No factory request, RTC setting, Flash/UICR write or reset was performed.'
        exit 0
    }

    Assert-True ((U32 $before 8) -ne 1) 'A previous factory request is still running. Do not start a second request.'

    $requestTime = Get-Date
    Assert-True (($requestTime.Year -ge 2000) -and ($requestTime.Year -le 2099)) 'Set the PC clock to a valid year from 2000 to 2099.'
    $rtc = @($requestTime.Second, $requestTime.Minute, $requestTime.Hour, $requestTime.Day, $requestTime.Month, ($requestTime.Year - 2000))
    $commands = @('Halt', 'W4 0x2000FFC0, 0', 'W4 0x2000FFC8, 0', 'W4 0x2000FFE8, 0')
    for ($i = 0; $i -lt 6; $i++) { $commands += ('W1 0x{0:X8}, 0x{1:X2}' -f ($Mailbox + 32 + $i), $rtc[$i]) }
    $commands += @('W4 0x2000FFC4, 0xA6F01601', 'W4 0x2000FFC0, 0x31544B50', 'W4 0xE000E200, 0x00100000', 'Go', 'Sleep 1500', 'Halt',
        'SaveBin mailbox_after.bin, 0x2000FFC0, 0x40', 'SaveBin flags_after.bin, 0x2000536C, 0x08',
        'SaveBin delay_after.bin, 0x20005320, 0x04', 'Go', 'Exit')
    $null = Invoke-JLink 'Request_and_readback_factory_mode' $commands
    $after = Read-Binary 'mailbox_after.bin' 64
    $flags = Read-Binary 'flags_after.bin' 8
    $delay = Read-Binary 'delay_after.bin' 4
    $status = U32 $after 8
    $statusMeaning = @{
        0 = 'request not processed'; 1 = 'request still running'; 2 = 'ready';
        225 = 'RTC write failed'; 226 = 'RTC read/validity failed'; 227 = 'charging coil detected';
        228 = 'power-saving state active'; 229 = 'invalid requested date'
    }
    $meaning = 'unknown status'
    if (($status -le [int]::MaxValue) -and $statusMeaning.ContainsKey([int]$status)) { $meaning = $statusMeaning[[int]$status] }
    Assert-True ($status -eq 2) ('Factory mode failed: status 0x{0:X8} ({1}). RTC write={2}, read={3}.' -f $status, $meaning, (U32 $after 12), (U32 $after 16))
    Assert-True ((U32 $after 60) -eq $BootStamp) 'Startup stamp changed during the test.'
    Assert-True ((U32 $after 0) -eq 0) 'The request was not consumed.'
    Assert-True ((U32 $after 40) -eq $Cookie) 'The factory acknowledgement cookie is invalid.'
    Assert-True (((U32 $after 12) -eq 0) -and ((U32 $after 16) -eq 0)) 'RTC write/read did not return success.'
    Assert-True (((U32 $after 20) -eq $ExpectedTicks) -and ((U32 $delay 0) -eq $ExpectedTicks)) 'The RFID polling period is not 45875 RTC ticks (1400 ms).'
    Assert-True ($flags[3] -eq 1) 'The active flag was not set in RAM.'
    Assert-True ($flags[0] -eq 0) 'The board reports charging-coil power. Remove the station/coil before measuring.'
    Assert-True ($flags[4] -eq 0) 'The board is in power-saving mode. Check the supply voltage before measuring.'
    $clockText = '{0:D4}{1:D2}{2:D2}{3:D2}{4:D2}{5:D2}' -f (2000 + [int]$after[29]), [int]$after[28], [int]$after[27], [int]$after[26], [int]$after[25], [int]$after[24]
    $clock = [DateTime]::ParseExact($clockText, 'yyyyMMddHHmmss', [Globalization.CultureInfo]::InvariantCulture)
    Assert-True ([Math]::Abs(((Get-Date) - $clock).TotalSeconds) -le 120) 'RTC readback is not close to the PC time.'

    Release-DebugPower 'Release_debug_power_FINAL'
    # Host-only reporting below. No target access after Release-DebugPower.
    $report = [ordered]@{
        success = $true; operation = 'prepare_ppk_test'; probe_serial = $script:Probe; completed_local = (Get-Date).ToString('o');
        firmware = $FirmwareName; flash_performed = [bool]$Flash; recover_performed = [bool]$Recover;
        factory_status = $status; rfid_ticks = (U32 $delay 0); active_ram = [int]$flags[3];
        coil_ram = [int]$flags[0]; power_saving_ram = [int]$flags[4]; rtc_readback_local = $clock.ToString('s');
        debug_power_release_commands_completed = $true;
        hardware_validation_note = 'Register/RTC readback passed; validate the actual current waveform with PPK2.'
    }
    [IO.File]::WriteAllText((Join-Path $script:OutDir 'result.json'), ($report | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
    Write-Host ''
    Write-Host 'READY FOR PPK MEASUREMENT.' -ForegroundColor Green
    Write-Host 'Disconnect SWD without interrupting PPK power. Do not open a debugger or run another read command while measuring.'
    Write-Host 'A reset/power cycle clears this one-time request. Existing external activation/settings are preserved.'
    exit 0
} catch {
    $message = $_.Exception.Message
    Write-Host ('TEST FAILED: ' + $message) -ForegroundColor Red
    if ($script:OutDir) {
        [IO.File]::WriteAllText((Join-Path $script:OutDir 'FAILED.txt'), $message, (New-Object Text.UTF8Encoding($false)))
        Write-Host ('Inspect logs: ' + $script:OutDir)
    }
    if ($script:JLinkTouched -and -not $script:Released) {
        try { Release-DebugPower 'Release_debug_power_after_failure' } catch {
            Write-Host 'Could not release the debug connection. Close debug programs and disconnect SWD before further work.' -ForegroundColor Yellow
        }
    }
    Write-Host 'Do not classify the board from a low current value after a failed preparation.' -ForegroundColor Yellow
    exit 1
}
