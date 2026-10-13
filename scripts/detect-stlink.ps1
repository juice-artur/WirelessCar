# detect-stlink.ps1
#
# Determines which physical ST-Link is attached to the STM32 and which one to
# the nRF52, and generates the artifacts that remove the need to edit configs
# by hand:
#
#   ProjectAstra/AstraSTM32/AstraApplication/.vscode/stlink.json
#   ProjectAstra/AstraSTM32/AstraBootloader/.vscode/stlink.json
#       { "stm": "<sn>", "nrf": "<sn>" }  - read by the Command Variable
#       extension (extension.commandvariable.file.content) from launch.json.
#
#   ProjectAstra/AstranRF52/openocd/stlink_serial.local.cfg
#       "adapter serial <sn>"  - conditionally sourced by stlink_nrf52840.cfg
#
# Run (after physically swapping probes, with no active debug session):
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/detect-stlink.ps1
#
# Both probes share VID:PID 0483:3748, so the only reliable discriminator is the
# serial: the one under which STM32CubeProgrammer sees an STM32G4 target is the
# STM32. The other probe is the nRF52 (by elimination, because CubeProgrammer
# cannot positively identify an nRF target).

[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot

# --- 1. Locate the newest STM32_Programmer_CLI.exe --------------------------
$cliRoot = Join-Path $env:LOCALAPPDATA 'stm32cube\bundles\programmer'
$cli = Get-ChildItem $cliRoot -Directory -ErrorAction SilentlyContinue |
    Sort-Object {
        try { [version]($_.Name -replace '[^0-9.].*$', '') } catch { [version]'0.0' }
    } -Descending |
    ForEach-Object { Join-Path $_.FullName 'bin\STM32_Programmer_CLI.exe' } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1

if (-not $cli) { throw "STM32_Programmer_CLI.exe not found under $cliRoot" }
Write-Host "CLI: $cli"

# --- 2. Enumerate connected probes ------------------------------------------
$list = & $cli '--list' 2>&1 | Out-String
$serials = @(
    [regex]::Matches($list, 'ST-LINK SN\s*:\s*(\w+)') |
        ForEach-Object { $_.Groups[1].Value } |
        Select-Object -Unique
)

if (-not $serials) { throw "No ST-Link probes found. Check the USB connection." }
Write-Host ("Probes found: " + ($serials -join ', '))

# --- 3. Classify ------------------------------------------------------------
# Try hotplug first, then under-reset (needs NRST wired; harmless otherwise).
$stm = $null
foreach ($sn in $serials) {
    foreach ($mode in 'hotplug', 'underReset') {
        $out = & $cli '-c' 'port=SWD' "sn=$sn" "mode=$mode" 2>&1 | Out-String
        if ($out -match 'STM32G4') { $stm = $sn; break }
    }
    if ($stm) { break }
}

if (-not $stm) {
    Write-Warning "No ST-Link with an STM32 target was detected (probe busy with an active debug session, or SWD / target power is down)."
    Write-Warning "stlink.json / stlink_serial.local.cfg were NOT modified to avoid breaking a working setup."
    Write-Warning "Close debug sessions (kill stale cube.exe if needed) and run the detect again."
    exit 1
}

# The second probe (when exactly two are present) is the nRF52: determined by
# elimination, because CubeProgrammer cannot positively identify an nRF target.
$nrf = if (@($serials).Count -ge 2) {
    $serials | Where-Object { $_ -ne $stm } | Select-Object -First 1
}
else { $null }

if (-not $nrf) { Write-Warning "The second ST-Link (nRF52) could not be determined; OpenOCD will use the first probe." }
if (-not $nrf) { $nrf = '' }   # never write JSON null

# --- 4. stlink.json (for Command Variable) ----------------------------------
$json = [pscustomobject]@{ stm = $stm; nrf = $nrf } | ConvertTo-Json -Compress
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$jsonTargets = @(
    'ProjectAstra\AstraSTM32\AstraApplication\.vscode\stlink.json',
    'ProjectAstra\AstraSTM32\AstraBootloader\.vscode\stlink.json'
)
foreach ($rel in $jsonTargets) {
    $path = Join-Path $root $rel
    New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
    [System.IO.File]::WriteAllText($path, $json, $utf8NoBom)
    Write-Host "Wrote: $path"
}

# --- 5. stlink_serial.local.cfg (for OpenOCD) -------------------------------
$cfgPath = Join-Path $root 'ProjectAstra\AstranRF52\openocd\stlink_serial.local.cfg'
if ($nrf) { $cfgBody = "adapter serial $nrf`n" }
else { $cfgBody = "# no nRF ST-Link detected; OpenOCD will use the first probe`n" }
Set-Content -Path $cfgPath -Value $cfgBody -Encoding ascii
Write-Host "Wrote: $cfgPath"

Write-Host ''
Write-Host "  STM32 <- $stm"
Write-Host "  nRF52 <- $nrf"
