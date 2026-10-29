# Windows PowerShell 5.1. Permanently clears external identity, then installs exact approved 1.6.
[CmdletBinding()]
param([string]$ProbeSerial = '', [string]$JLinkPath = '', [switch]$ValidateOnly)
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
$ServiceName = 'VGL_Patrol_1.6_IDENTITY_RESET_SERVICE_FULL_S132_BOOTLOADER.hex'
$ApprovedName = 'VGL_Patrol_1.6_1400ms_RF5ms_FULL_S132_BOOTLOADER.hex'
$ApprovedSha256 = '7feae35a573f95cbc75a5cc83dfdf086ceed9f619afa0f101804f756451c11dc'
$BootStamp = [uint32]0x31525342
$RequestMagic = [uint32]0x31525346
$Cookie = [Convert]::ToUInt32('A6F01602', 16)

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
    Assert-True ($manifest.firmware_file -ceq $ServiceName) 'This manifest belongs to a different reset package.'
    Assert-True (@($manifest.files).Count -gt 0) 'The manifest has no files.'
    $foundService = $false
    $foundApproved = $false
    foreach ($entry in $manifest.files) {
        $name = [string]$entry.file
        Assert-True (($name -ne '') -and ([IO.Path]::GetFileName($name) -ceq $name) -and ($name -notmatch '[\\/:]')) 'Unsafe filename in manifest.'
        Assert-True ([string]$entry.sha256 -match '^[0-9a-fA-F]{64}$') ('Invalid SHA-256 for ' + $name)
        $path = Join-Path $script:Root $name
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) ('Package file is missing: ' + $name)
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        Assert-True ($actual -ieq [string]$entry.sha256) ('SHA-256 mismatch: ' + $name + '. Do not use this package.')
        if ($name -ceq $ServiceName) { $foundService = $true }
        if ($name -ceq $ApprovedName) {
            Assert-True ($actual -ieq $ApprovedSha256) 'The approved image is not the unchanged owner-approved ordinary 1.6 FULL HEX.'
            $foundApproved = $true
        }
    }
    Assert-True ($foundService -and $foundApproved) 'The service and unchanged ordinary firmware must both be included in the checksums.'
}

function Assert-DefaultIdentity([byte[]]$Bytes) {
    Assert-True ($Bytes.Length -eq 23) 'Identity verification has an unexpected length.'
    Assert-True ($Bytes[0] -eq 0) 'The persistent activation flag was not cleared.'
    Assert-True (@($Bytes[1..22] | Where-Object { $_ -ne 0x30 }).Count -eq 0) 'The persistent secret or number was not reset to the original unconfigured defaults.'
}

function Release-DebugPower([string]$Name) {
    # This MUST be the final target access. Do not reconnect, reset or read after it.
    # Earlier sessions resume the CPU. Commander 7.94e rejects Go on an already
    # running core, so establish Halt before Go without resetting RAM or RTC.
    $null = Invoke-JLink $Name @('Halt', 'Go', 'WriteDP 2, 0', 'WriteDP 1, 0', 'Exit')
    $script:Released = $true
}

try {
    Validate-Package
    if ($ValidateOnly) { Write-Host 'PACKAGE CHECK PASSED. No hardware was accessed.'; exit 0 }
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
    Write-Host 'This explicitly erases the processor and permanently clears activation, secret and number in external Flash.' -ForegroundColor Yellow
    Write-Host 'Keep board power connected until FACTORY_RESET_COMPLETE. Existing event data/counters are not intentionally erased.'

    $null = Invoke-Nrf @('--recover') 'Recover_processor'
    $oldUicr = Read-UicrBootAddress
    Assert-True (($oldUicr -eq [uint32]::MaxValue) -or ($oldUicr -eq [uint32]0x00078000)) 'Unexpected UICR bootloader address after recover.'
    $null = Invoke-Nrf @('--program', (Join-Path $script:Root $ServiceName), '--sectorerase', '--verify') 'Program_reset_service'
    if ($oldUicr -eq [uint32]::MaxValue) {
        $null = Invoke-Nrf @('--memwr', '0x10001014', '--val', '0x00078000', '--verify') 'Restore_verified_bootloader_address'
    }
    Assert-True ((Read-UicrBootAddress) -eq [uint32]0x00078000) 'UICR bootloader-address readback failed.'
    $null = Invoke-Nrf @('--reset') 'Start_reset_service'
    Start-Sleep -Milliseconds 2000
    $null = Invoke-Nrf @('--verify', (Join-Path $script:Root $ServiceName)) 'Verify_service_after_boot'
    $null = Invoke-JLink 'Read_service_startup' @('Halt', 'SaveBin reset_header_before.bin, 0x2000FFC0, 0x20', 'SaveBin reset_stamp_before.bin, 0x2000FFFC, 0x04', 'SaveBin nvic_iser0.bin, 0xE000E100, 0x04', 'Go', 'Exit')
    $header = Read-Binary 'reset_header_before.bin' 32
    $stamp = Read-Binary 'reset_stamp_before.bin' 4
    $iser = Read-Binary 'nvic_iser0.bin' 4
    Assert-True ((U32 $stamp 0) -eq $BootStamp) 'The reset-service startup stamp is absent. The ordinary image has not been restored; inspect startup and LED errors.'
    Assert-True (((U32 $iser 0) -band [uint32]0x00100000) -ne 0) 'The existing app_timer SWI0 IRQ20 is disabled. The driver will not enable it.'
    Assert-True (((U32 $header 0) -eq 0) -and ((U32 $header 8) -eq 0) -and ((U32 $header 24) -eq 0)) 'The reset service is not in its fresh idle state.'
    $null = Invoke-JLink 'Request_persistent_identity_reset' @('Halt', 'W4 0x2000FFC4, 0xA6F01602', 'W4 0x2000FFC0, 0x31525346', 'W4 0xE000E200, 0x00100000', 'Go', 'Sleep 3000', 'Halt', 'SaveBin reset_header_after.bin, 0x2000FFC0, 0x20', 'SaveBin reset_stamp_after.bin, 0x2000FFFC, 0x04', 'Go', 'Exit')
    $header = Read-Binary 'reset_header_after.bin' 32
    $stamp = Read-Binary 'reset_stamp_after.bin' 4
    $poll = 0
    while (((U32 $header 8) -eq 1) -and ($poll -lt 4)) {
        $poll++
        $null = Invoke-JLink ('Wait_reset_completion_' + $poll) @('Halt', 'Go', 'Sleep 2000', 'Halt', 'SaveBin reset_header_after.bin, 0x2000FFC0, 0x20', 'SaveBin reset_stamp_after.bin, 0x2000FFFC, 0x04', 'Go', 'Exit')
        $header = Read-Binary 'reset_header_after.bin' 32
        $stamp = Read-Binary 'reset_stamp_after.bin' 4
    }
    $status = U32 $header 8
    $meaning = @{ 0='request was not processed'; 1='reset is still running'; 2='verified'; 225='external Flash write failed'; 226='external Flash read failed'; 227='persistent identity readback mismatch'; 228='charging coil detected'; 229='power-saving mode active' }
    $statusText = 'unknown'
    if (($status -le [int]::MaxValue) -and $meaning.ContainsKey([int]$status)) { $statusText = $meaning[[int]$status] }
    Assert-True ($status -eq 2) ('Identity reset failed: status 0x{0:X8} ({1}). The ordinary firmware has not been restored.' -f $status, $statusText)
    Assert-True ((U32 $stamp 0) -eq $BootStamp) 'The reset-service startup stamp changed.'
    Assert-True ((U32 $header 0) -eq 0) 'The one-time reset request was not consumed.'
    Assert-True ((U32 $header 24) -eq $Cookie) 'The reset acknowledgement is invalid.'
    Assert-True (((U32 $header 12) -eq 0) -and ((U32 $header 16) -eq 0)) 'The persistent identity write/read did not return success.'
    Assert-True ((U32 $header 20) -eq 23) 'The reset service did not verify all 23 identity bytes.'
    # Do not dump readback scratch on failed reset: it could contain an old secret.
    $null = Invoke-JLink 'Read_verified_cleared_identity' @('Halt', 'SaveBin cleared_identity.bin, 0x2000FFE0, 0x17', 'Go', 'Exit')
    $identity = Read-Binary 'cleared_identity.bin' 23
    Assert-DefaultIdentity $identity

    # Replace the service application with the exact ordinary owner-approved image.
    $null = Invoke-Nrf @('--program', (Join-Path $script:Root $ApprovedName), '--sectorerase', '--verify') 'Program_ordinary_approved_1_6'
    Assert-True ((Read-UicrBootAddress) -eq [uint32]0x00078000) 'UICR bootloader address changed before final startup.'
    $null = Invoke-Nrf @('--reset') 'Start_ordinary_firmware'
    Start-Sleep -Milliseconds 2000
    $null = Invoke-Nrf @('--verify', (Join-Path $script:Root $ApprovedName)) 'Verify_ordinary_firmware_after_boot'
    $null = Invoke-JLink 'Verify_ordinary_unconfigured_state' @('Halt', 'SaveBin ordinary_flags.bin, 0x2000536C, 0x08', 'SaveBin ordinary_identity.bin, 0x20002A88, 0x16', 'Go', 'Exit')
    $flags = Read-Binary 'ordinary_flags.bin' 8
    $ordinaryIdentity = Read-Binary 'ordinary_identity.bin' 22
    Assert-True ($flags[3] -eq 0) 'The ordinary firmware did not start inactive.'
    Assert-True ($flags[0] -eq 0) 'The board reports coil power; remove the station/coil to verify the inactive no-polling state.'
    Assert-True (@($ordinaryIdentity | Where-Object { $_ -ne 0x30 }).Count -eq 0) 'The ordinary firmware did not load the unconfigured secret and number.'
    Release-DebugPower 'Release_debug_power_FINAL'
    # No target access follows this point.
    $report = [ordered]@{ success=$true; operation='reset_external_identity_and_restore_ordinary'; probe_serial=$script:Probe; completed_local=(Get-Date).ToString('o'); final_firmware=$ApprovedName; final_sha256=$ApprovedSha256; external_identity_verified=$true; active_ram=0; secret_and_number_unconfigured=$true; debug_power_release_commands_completed=$true; scope='Activation,16-byte secret and6-byte serial reset; existing event records/counters not intentionally erased.' }
    [IO.File]::WriteAllText((Join-Path $script:OutDir 'result.json'), ($report | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
    Write-Host ''
    Write-Host 'FACTORY_RESET_COMPLETE.' -ForegroundColor Green
    Write-Host 'Ordinary approved 1.6 is installed. Activation, secret and number are reset and verified.'
    Write-Host 'Disconnect SWD. The board is ready for normal provisioning through the station.'
    exit 0
} catch {
    $message = $_.Exception.Message
    Write-Host ('RESET FAILED: ' + $message) -ForegroundColor Red
    if ($script:OutDir) {
        [IO.File]::WriteAllText((Join-Path $script:OutDir 'FAILED.txt'), $message, (New-Object Text.UTF8Encoding($false)))
        Write-Host ('Inspect logs: ' + $script:OutDir)
    }
    if ($script:JLinkTouched -and -not $script:Released) {
        try { Release-DebugPower 'Release_debug_power_after_failure' } catch {
            Write-Host 'Could not release the debug connection. Close debug programs and disconnect SWD before further work.' -ForegroundColor Yellow
        }
    }
    Write-Host 'Reset was not confirmed. Do not treat this board as factory-reset or infer its state from current consumption.' -ForegroundColor Yellow
    exit 1
}
