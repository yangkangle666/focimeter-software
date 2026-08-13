$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$system = Join-Path $repo "focimeter_system"
$m2Linux = "/root/focimeter-m2-meeting-build/focimeter_m2"
$runId = "meeting_demo_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
$runRoot = Join-Path $system "outputs\$runId"
$m1RequestRoot = Join-Path $runRoot "requests"

function Convert-ToWslPath([string]$path) {
    $resolved = (Resolve-Path -LiteralPath $path).Path
    if ($resolved -notmatch '^([A-Za-z]):\\(.*)$') {
        throw "Only Windows local paths are supported: $resolved"
    }
    return "/mnt/$($Matches[1].ToLower())/$($Matches[2] -replace '\\','/')"
}

function Invoke-M1([string]$lensId) {
    $requestPath = Join-Path $m1RequestRoot "request_$lensId.json"
    $request = [ordered]@{
        schema_version = "1.0"
        task_id = "${runId}_$lensId"
        module = "m1_input_config"
        status = "ok"
        request = [ordered]@{
            calibration_image = "data/real/multispot_lens_pairs/real_lens_pair_set_001/images/reference_no_lens.jpg"
            measurement_image = "data/real/multispot_lens_pairs/real_lens_pair_set_001/images/lens_${lensId}_spots.jpg"
            config_path = "data/real/multispot_lens_pairs/real_lens_pair_set_001/config/detection_config.json"
            run_mode = "local_image"
            operator = "meeting_demo"
            notes = "Phase-one real-data software integration demo; not metrology validation."
        }
        error = $null
    }
    New-Item -ItemType Directory -Force -Path $m1RequestRoot | Out-Null
    $request | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $requestPath -Encoding UTF8
    $packagePath = Join-Path (Join-Path $runRoot $lensId) "input_package.json"
    $null = python -m modules.input_config.cli --input $requestPath --project-root $system --output $packagePath
    if ($LASTEXITCODE -ne 0) { throw "M1 failed: $lensId" }
    return $packagePath
}

function Invoke-M2([string]$lensId, [string]$packagePath) {
    $outputPath = Join-Path (Join-Path $runRoot $lensId) "m2"
    New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
    $wslPackage = Convert-ToWslPath $packagePath
    $wslOutput = Convert-ToWslPath $outputPath
    $wslSystem = Convert-ToWslPath $system
    $m2Log = Join-Path $outputPath "m2_console.log"
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $null = wsl -d Ubuntu -- $m2Linux --input $wslPackage --output $wslOutput --project-root $wslSystem --experimental-multispot --save-intermediate 2> $m2Log
        $m2ExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($m2ExitCode -ne 0) { throw "M2 failed: $lensId (exit $m2ExitCode)" }
    $experimental = Join-Path $outputPath "experimental_multispot"
    $calibration = Join-Path $experimental "spots_calib_multispot.json"
    $measurement = Join-Path $experimental "spots_meas_multispot.json"
    if (-not (Test-Path -LiteralPath $calibration) -or -not (Test-Path -LiteralPath $measurement)) {
        throw "M2 did not create experimental multispot JSON: $lensId"
    }
    return @($calibration, $measurement)
}

function Invoke-M3([string]$lensId, [string[]]$m2Files) {
    $config = Join-Path $system "config\default_config.json"
    $model = Join-Path $system "modules\calibration_calculation\examples\calibration\calibration_model.simulation.json"
    $resultPath = Join-Path (Join-Path $runRoot $lensId) "m3_result.json"
    $m3Console = python -m modules.calibration_calculation.algorithm.cli calculate `
        --calibration $m2Files[0] `
        --measurement $m2Files[1] `
        --config $config `
        --model $model `
        --engineering-mode
    if ($LASTEXITCODE -ne 0) { throw "M3 failed: $lensId" }
    $m3Text = $m3Console -join [Environment]::NewLine
    $m3Text | Set-Content -LiteralPath $resultPath -Encoding UTF8
    return $m3Text | ConvertFrom-Json
}

Write-Host "Focimeter phase-one real-data demo" -ForegroundColor Cyan
Write-Host "Run directory: $runRoot"
Write-Host "Scope: M1/M2/M3 software integration; M3 engineering mode is not metrology validation." -ForegroundColor Yellow

Push-Location $system
try {

foreach ($lensId in @("001", "002")) {
    Write-Host "`n[$lensId] M1 input and path validation" -ForegroundColor Green
    $package = Invoke-M1 $lensId
    $packageJson = Get-Content -LiteralPath $package -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Host "M1: $($packageJson.status), paths_checked=$($packageJson.quality.paths_checked)"

    Write-Host "[$lensId] M2 spot detection" -ForegroundColor Green
    $m2Files = Invoke-M2 $lensId $package
    $m2Calibration = Get-Content -LiteralPath $m2Files[0] -Raw -Encoding UTF8 | ConvertFrom-Json
    $m2Measurement = Get-Content -LiteralPath $m2Files[1] -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Host "M2: calibration=$($m2Calibration.spots.Count), measurement=$($m2Measurement.spots.Count)"

    Write-Host "[$lensId] M3 S/C/A calculation" -ForegroundColor Green
    $m3 = Invoke-M3 $lensId $m2Files
    Write-Host ("M3: status={0}, S={1:N5} D, C={2:N5} D, A={3}, matched={4}/{5}, validation={6}" -f `
        $m3.status, [double]$m3.result.S, [double]$m3.result.C, $m3.result.A, `
        $m3.matching.matched_spot_count, $m3.matching.measurement_detection_count, $m3.quality.validation_status) -ForegroundColor White
}

Write-Host "`nDemo completed. Full outputs: $runRoot" -ForegroundColor Cyan
}
finally {
    Pop-Location
}
