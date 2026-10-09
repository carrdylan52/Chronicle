# Launch a bounded DCVR test. All run output and game writes go into a fresh candidate folder.
param(
    [ValidateSet('Room', 'Norune', 'Synthetic')][string]$Mode = 'Room',
    [string]$BuildDirectory,
    [string]$DataDirectory = 'C:/Dylan/_scratch/chronicle-win/data',
    [string]$SeedSave,
    [ValidateRange(1,600)][int]$Seconds = 120,
    [ValidateRange(1,100)][float]$UnitsPerMetre = 10,
    [ValidateRange(1,10000)][int]$Frames = 260,
    [string]$InputScript
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$BuildDirectory) { $BuildDirectory = Join-Path $source 'build/dcvr' }
if (!$SeedSave) { $SeedSave = Join-Path $source 'build/validation/save-baseline' }
if ($Mode -eq 'Synthetic' -and !$InputScript) { $InputScript = Join-Path $source 'tools/dcvr/norune-still.input' }
$build = [IO.Path]::GetFullPath($BuildDirectory)
$output = [IO.Path]::GetFullPath((Join-Path $build 'vr-tests'))
# An explicitly selected build directory owns its test output; no existing run is overwritten.
if (!$output.StartsWith($build + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Test output must be inside the selected build directory.'
}
$program = Join-Path $build $(if ($Mode -eq 'Room') { 'dcvr_room.exe' } else { 'dcvr_game.exe' })
if (!(Test-Path -LiteralPath $program)) { throw 'Build with -OpenXR first (docs/DCVR.md).' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
$run = Join-Path $output ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $Mode.ToLower() + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "DCVR $Mode test. Receipts: $run"
if ($Mode -ne 'Synthetic') {
    $probe = Join-Path $build 'dcvr_probe.exe'
    & $probe > (Join-Path $run 'openxr.json') 2> (Join-Path $run 'openxr.log')
    if ($LASTEXITCODE -ne 0) {
        $diagnostic = Get-Content -LiteralPath (Join-Path $run 'openxr.json') -Raw | ConvertFrom-Json
        Write-Host "OpenXR is not ready: $($diagnostic.failure.stage) ($($diagnostic.failure.code))."
        Write-Host 'In Quest: open Quest Link, choose this PC, Pair using Air Link, then Launch.'
        Write-Host 'Leave Meta Horizon Link running on the PC. This script does not change runtimes.'
        exit 1
    }
}
if ($Mode -eq 'Room') {
    $arguments = @('--seconds', "$Seconds", '--frames', '100000')
    Write-Host 'Calibration: put on Quest. WASD walk; Q/E turn; R recenter; Esc exit.'
} else {
    if (!(Test-Path -LiteralPath $DataDirectory -PathType Container)) { throw 'Game data directory is missing.' }
    if (!(Test-Path -LiteralPath $SeedSave -PathType Container)) { throw 'Provide -SeedSave with an existing candidate save folder to copy.' }
    $candidate = Join-Path $run 'save'
    Copy-Item -LiteralPath $SeedSave -Destination $candidate -Recurse
    $configPath = Join-Path $candidate 'config.json'
    if (Test-Path -LiteralPath $configPath) {
        $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    } else { $config = [pscustomobject]@{} }
    if (!$config.game) { $config | Add-Member -NotePropertyName game -NotePropertyValue ([pscustomobject]@{}) }
    if (!$config.input) { $config | Add-Member -NotePropertyName input -NotePropertyValue ([pscustomobject]@{}) }
    if (!$config.video) { $config | Add-Member -NotePropertyName video -NotePropertyValue ([pscustomobject]@{}) }
    $config.game | Add-Member -NotePropertyName tick_rate -NotePropertyValue 60 -Force
    $config.input | Add-Member -NotePropertyName mouse_zoom -NotePropertyValue $true -Force
    $config.video | Add-Member -NotePropertyName soft_focus -NotePropertyValue $false -Force
    $config.video | Add-Member -NotePropertyName fullscreen -NotePropertyValue $false -Force
    $config | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $configPath -Encoding utf8
    $arguments = @('--data', $DataDirectory, '--save', $candidate, '--vr-seconds', "$Seconds",
                   '--vr-scale', $UnitsPerMetre.ToString([Globalization.CultureInfo]::InvariantCulture),
                   '--width', '960', '--height', '720')
    if ($Mode -eq 'Synthetic') {
        $arguments += '--vr-synthetic', (Join-Path $run 'capture'), '--frames', "$Frames"
    }
    if ($InputScript) { $arguments += '--input', (Get-Item -LiteralPath $InputScript).FullName }
    Write-Host 'Norune: WASD / ordinary gamepad move Toan; mouse / right stick control the game camera.'
    Write-Host 'F9 recenters your head; Esc exits. Keep the DCVR window focused. No Quest controller bindings yet.'
    Write-Host 'First test: stay in Norune walking mode; HUD, menus, water and other modes are unfinished.'
}
# ProcessStartInfo quotes each argument independently, including paths with spaces/Unicode.
$launch = [Diagnostics.ProcessStartInfo]::new($program)
$launch.UseShellExecute = $false
$launch.WorkingDirectory = $source
$launch.RedirectStandardOutput = $true
$launch.RedirectStandardError = $true
$launch.CreateNoWindow = $true
foreach ($argument in $arguments) { $launch.ArgumentList.Add($argument) }
if ($Mode -eq 'Synthetic') { $launch.Environment['DC_AUDIO'] = 'off' }
$process = [Diagnostics.Process]::Start($launch)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
# A broken runtime can stall an XR call. Only this launcher-owned child is terminated on timeout.
if (!$process.WaitForExit(($Seconds + 45) * 1000)) {
    $process.Kill()
    $process.WaitForExit()
    $timedOut = $true
} else { $timedOut = $false }
$stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $run 'stdout.log') -Encoding utf8
$stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $run 'stderr.log') -Encoding utf8
$exitCode = $process.ExitCode
$process.Dispose()
[ordered]@{ mode=$Mode; exit_code=$exitCode; timed_out=$timedOut; headset_validated=$false;
             units_per_metre=$UnitsPerMetre; source_commit=(& git -C $source rev-parse HEAD);
             working_tree_dirty=[bool](& git -C $source status --porcelain);
             finished_utc=[DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'launcher.json') -Encoding utf8
Write-Host "Test exited $exitCode. Logs: $run"
if ($timedOut) { Write-Host 'The owned test process timed out and was stopped. Read stderr.log.'; exit 1 }
exit $exitCode
