param(
    [string]$ExecutablePath = (Join-Path $PSScriptRoot "..\..\bin\x64\Debug\RtPbrSurvey.exe"),
    [string]$ManifestPath = (Join-Path $PSScriptRoot "multi_scene_quality_gates.json"),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "results\multi-scene-quality-gates"),
    [switch]$KeepGoing
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$executable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$manifestFile = (Resolve-Path -LiteralPath $ManifestPath).Path
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$manifest = Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1)
{
    throw "Unsupported multi-scene quality-gate manifest schema $($manifest.schemaVersion)."
}

$caseResults = [System.Collections.Generic.List[object]]::new()
foreach ($case in $manifest.cases)
{
    $reportPath = Join-Path $output "$($case.id).json"
    $logPath = Join-Path $output "$($case.id)-d3d12.log"
    Remove-Item -LiteralPath $reportPath, $logPath -Force -ErrorAction SilentlyContinue

    $sceneFlag = switch ($case.scene)
    {
        "estimator-test" { "-AutoSelectHybridReflectionEstimatorTest" }
        "damaged-helmet" { "-AutoSelectGltfDamagedHelmet" }
        default { throw "Unsupported scene id $($case.scene)." }
    }
    $arguments = @(
        $sceneFlag,
        "-ReflectionHdrDiagnostics", $reportPath,
        "-ReflectionHdrDiagnosticsWarmupFrames", $manifest.common.warmupFrames,
        "-ReflectionHdrDiagnosticsFrames", $case.measurementFrames,
        "-ReflectionHdrDiagnosticsRoi", $case.roi.x, $case.roi.y, $case.roi.width, $case.roi.height,
        "-ReflectionCameraDistanceScale", $case.cameraDistanceScale,
        "-ReflectionTemporalWeight", $manifest.common.temporalHistoryWeight,
        "-ReflectionOrbitDegrees", $case.orbitDegrees,
        "-ReflectionOrbitFrames", $case.orbitFramesPerDirection,
        "-LogToFile", $logPath
    )
    if ($manifest.common.stochasticSampling)
    {
        $arguments += "-ReflectionStochasticSampling"
    }

    Write-Host "Running $($case.id)..."
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden
    $failures = [System.Collections.Generic.List[string]]::new()
    if ($process.ExitCode -ne 0)
    {
        $failures.Add("process exit code $($process.ExitCode)")
    }
    if (-not (Test-Path -LiteralPath $reportPath))
    {
        $failures.Add("report was not created")
    }

    $report = $null
    $contractValidation = $null
    if ($failures.Count -eq 0)
    {
        $report = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $validatorOutput = (& (Join-Path $PSScriptRoot "Test-DynamicTemporalReport.ps1") -ReportPath $reportPath) -join
            [Environment]::NewLine
        $contractValidation = $validatorOutput | ConvertFrom-Json
        if ($contractValidation.status -ne "pass")
        {
            $failures.Add("dynamic report contract validation failed")
        }
    }

    $errorMessages = @()
    $warningMessages = @()
    if (Test-Path -LiteralPath $logPath)
    {
        $errorMessages = @(Select-String -LiteralPath $logPath -Pattern "\[ERROR\]" | ForEach-Object { $_.Line })
        $warningMessages = @(Select-String -LiteralPath $logPath -Pattern "\[WARNING\]" | ForEach-Object { $_.Line })
    }
    if ($errorMessages.Count -ne 0)
    {
        $failures.Add("D3D12 Debug Layer reported $($errorMessages.Count) errors")
    }
    $unknownWarnings = @($warningMessages | Where-Object { $_ -notmatch "Ignoring InitialState.*Buffers are effectively created" })
    if ($unknownWarnings.Count -ne 0)
    {
        $failures.Add("D3D12 Debug Layer reported $($unknownWarnings.Count) unknown warnings")
    }
    if ($warningMessages.Count -gt [int]$manifest.common.maximumKnownDebugWarnings)
    {
        $failures.Add("known warning count $($warningMessages.Count) exceeds the manifest limit")
    }

    $metrics = $null
    if ($null -ne $report)
    {
        $moving = @($report.frames | Where-Object { $_.cameraMotionPhase -ne "stationary" })
        $stationary = @($report.frames | Where-Object { $_.cameraMotionPhase -eq "stationary" })
        $movingOutsideRate = [double](
            $moving | Measure-Object outsideHistoryRate -Average).Average
        $stationaryAcceptance = [double](
            $stationary | Measure-Object temporalAcceptanceRate -Average).Average
        if ($stationaryAcceptance -lt [double]$manifest.common.minimumStationaryAcceptance)
        {
            $failures.Add("stationary acceptance $stationaryAcceptance is below the manifest minimum")
        }
        if ($movingOutsideRate -gt [double]$manifest.common.maximumMovingOutsideHistoryRate)
        {
            $failures.Add("moving outside-history rate $movingOutsideRate exceeds the manifest maximum")
        }
        if ($case.requireSettlingMetric -and -not [bool]$report.settlingDiagnostic.valid)
        {
            $failures.Add("a valid settling metric is required")
        }
        if ([bool]$report.settlingDiagnostic.valid -and $null -ne $report.settlingDiagnostic.t95Frames -and
            [int]$report.settlingDiagnostic.t95Frames -gt [int]$case.maximumT95Frames)
        {
            $failures.Add("T95 exceeds the case maximum")
        }
        $metrics = [ordered]@{
            movingMotionMeanNdc = [double]($moving | Measure-Object meanMotionVectorMagnitudeNdc -Average).Average
            movingAcceptanceMean = [double]($moving | Measure-Object temporalAcceptanceRate -Average).Average
            movingOutsideHistoryMean = $movingOutsideRate
            movingDepthRejectMean = [double]($moving | Measure-Object depthRejectRate -Average).Average
            movingNormalRejectMean = [double]($moving | Measure-Object normalRejectRate -Average).Average
            stationaryAcceptanceMean = $stationaryAcceptance
            settlingValid = [bool]$report.settlingDiagnostic.valid
            t50Frames = $report.settlingDiagnostic.t50Frames
            t90Frames = $report.settlingDiagnostic.t90Frames
            t95Frames = $report.settlingDiagnostic.t95Frames
        }
    }

    $caseResult = [ordered]@{
        id = $case.id
        scene = $case.scene
        status = if ($failures.Count -eq 0) { "pass" } else { "fail" }
        failures = @($failures)
        processExitCode = $process.ExitCode
        reportPath = [System.IO.Path]::GetRelativePath($output, $reportPath)
        debugLayer = [ordered]@{
            errorCount = $errorMessages.Count
            warningCount = $warningMessages.Count
            unknownWarningCount = $unknownWarnings.Count
        }
        metrics = $metrics
    }
    $caseResults.Add($caseResult)
    if ($failures.Count -ne 0 -and -not $KeepGoing)
    {
        break
    }
}

$failedCases = @($caseResults | Where-Object { $_.status -ne "pass" })
$aggregate = [ordered]@{
    schemaVersion = 1
    suite = $manifest.name
    manifest = [System.IO.Path]::GetFileName($manifestFile)
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    signalDomain = $manifest.signalDomain
    physicalReference = [bool]$manifest.physicalReference
    perceptualQualityClaimed = [bool]$manifest.perceptualQualityClaimed
    status = if ($failedCases.Count -eq 0 -and $caseResults.Count -eq $manifest.cases.Count) { "pass" } else { "fail" }
    completedCaseCount = $caseResults.Count
    expectedCaseCount = $manifest.cases.Count
    cases = @($caseResults)
}
$aggregatePath = Join-Path $output "aggregate-report.json"
$aggregate | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $aggregatePath -Encoding utf8NoBOM
$aggregate | ConvertTo-Json -Depth 8
if ($aggregate.status -ne "pass")
{
    exit 1
}
