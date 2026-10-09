# Read-only machine inventory plus the optional compiled OpenXR probe. No runtime changes.
param([string]$BuildDirectory, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
# An unavailable runtime is an expected probe result, captured in the receipt below.
$PSNativeCommandUseErrorActionPreference = $false
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$BuildDirectory) { $BuildDirectory = Join-Path $source 'build/dcvr' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $source 'build/vr-diagnostics' }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$registrations = foreach ($view in @([Microsoft.Win32.RegistryView]::Registry64, [Microsoft.Win32.RegistryView]::Registry32)) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    try {
        $key = $base.OpenSubKey('SOFTWARE\Khronos\OpenXR\1')
        try {
            $active = if ($key) { $key.GetValue('ActiveRuntime') } else { $null }
            [ordered]@{ view = $view.ToString(); active_runtime = $active; manifest_exists = [bool]($active -and (Test-Path -LiteralPath $active)) }
        } finally { if ($key) { $key.Dispose() } }
    } finally { $base.Dispose() }
}
$report = [ordered]@{
    captured_utc = [DateTime]::UtcNow.ToString('o')
    runtime_changed = $false
    registrations = @($registrations)
    process_runtime_override = $env:XR_RUNTIME_JSON
    graphics = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    vr_processes = @(Get-Process | Where-Object ProcessName -Match '^(OVR|Oculus|vrserver|vrmonitor)' | Select-Object ProcessName)
    quest_devices = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object FriendlyName -Match 'Quest|Oculus|Rift' | Select-Object Status,Class,FriendlyName)
}
$probe = Join-Path $BuildDirectory 'dcvr_probe.exe'
if (Test-Path -LiteralPath $probe) {
    & $probe > (Join-Path $OutputDirectory 'openxr.json') 2> (Join-Path $OutputDirectory 'openxr.log')
    $report.probe_exit = $LASTEXITCODE
} else {
    $report.probe_exit = $null
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'machine.json') -Encoding utf8
Write-Output "Diagnostics: $OutputDirectory"
if ($null -eq $report.probe_exit) {
    Write-Output 'Build with -OpenXR to add the OpenXR/Vulkan probe.'
} elseif ($report.probe_exit -ne 0) {
    Write-Output 'OpenXR is not ready. Read openxr.json and docs/DCVR.md; this script has changed no runtime settings.'
    exit $report.probe_exit
} else {
    Write-Output 'Graphics prerequisites passed. This is not a headset rendering or comfort test.'
}
