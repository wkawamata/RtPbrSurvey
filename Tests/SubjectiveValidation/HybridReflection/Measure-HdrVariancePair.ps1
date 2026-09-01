param(
    [string]$ExecutablePath = (Join-Path $PSScriptRoot "..\..\..\bin\x64\Debug\RtPbrSurvey.exe"),
    [string]$OutputPath = (Join-Path $PSScriptRoot "hdr-variance-paired-report.json"),
    [int]$WarmupFrames = 32,
    [ValidateSet(64, 256, 1024)]
    [int]$MeasurementFrames = 64,
    [int]$RoiX = 895,
    [int]$RoiY = 278,
    [int]$RoiWidth = 75,
    [int]$RoiHeight = 85,
    [double]$TemporalWeight = 0.9,
    [ValidateSet("damaged-helmet", "estimator-test")]
    [string]$Scene = "damaged-helmet",
    [double]$CameraDistanceScale = 1.0,
    [switch]$CompareSpatiotemporalPolicy
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$executable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$output = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $output
if (-not (Test-Path -LiteralPath $outputDirectory))
{
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

$outputStem = [System.IO.Path]::GetFileNameWithoutExtension($output)
$offPath = Join-Path $outputDirectory "$outputStem-off.json"
$onPath = Join-Path $outputDirectory "$outputStem-on.json"

function Invoke-Diagnostic([string]$ReportPath, [bool]$FilterEnabled, [bool]$PolicyEnabled)
{
    if (Test-Path -LiteralPath $ReportPath)
    {
        Remove-Item -LiteralPath $ReportPath -Force
    }
    $sceneFlag = if ($Scene -eq "estimator-test")
    {
        "-AutoSelectHybridReflectionEstimatorTest"
    }
    else
    {
        "-AutoSelectGltfDamagedHelmet"
    }
    $arguments = @(
        $sceneFlag,
        "-ReflectionHdrDiagnostics", "`"$ReportPath`"",
        "-ReflectionHdrDiagnosticsWarmupFrames", $WarmupFrames,
        "-ReflectionHdrDiagnosticsFrames", $MeasurementFrames,
        "-ReflectionHdrDiagnosticsRoi", $RoiX, $RoiY, $RoiWidth, $RoiHeight,
        "-ReflectionCameraDistanceScale", $CameraDistanceScale,
        "-ReflectionStochasticSampling",
        "-ReflectionTemporalWeight", $TemporalWeight
    )
    if ($FilterEnabled)
    {
        $arguments += "-ReflectionSurfaceVarianceFilter"
    }
    if ($PolicyEnabled)
    {
        $arguments += "-ReflectionSpatiotemporalSpatialPolicy"
    }

    $process = Start-Process -FilePath $executable -ArgumentList $arguments -PassThru -Wait
    if ($process.ExitCode -ne 0)
    {
        throw "HDR diagnostic process failed with exit code $($process.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $ReportPath))
    {
        throw "HDR diagnostic process did not create $ReportPath."
    }
    return Get-Content -LiteralPath $ReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
}

$off = Invoke-Diagnostic $offPath $CompareSpatiotemporalPolicy.IsPresent $false
$on = Invoke-Diagnostic $onPath $true $CompareSpatiotemporalPolicy.IsPresent

$offSampling = @($off.frames | ForEach-Object { $_.samplingFrameIndex })
$onSampling = @($on.frames | ForEach-Object { $_.samplingFrameIndex })
$offTemporal = @($off.frames | ForEach-Object { $_.temporalFrameIndex })
$onTemporal = @($on.frames | ForEach-Object { $_.temporalFrameIndex })
$samplingDifferences = @(Compare-Object $offSampling $onSampling)
$temporalDifferences = @(Compare-Object $offTemporal $onTemporal)
$sampleSequenceMatches =
    $offSampling.Count -eq $onSampling.Count -and
    $samplingDifferences.Count -eq 0 -and
    $temporalDifferences.Count -eq 0

$offResolved = $off.statistics.resolvedRadiance
$onResolved = $on.statistics.resolvedRadiance
$offDenoised = $off.statistics.denoisedRadiance
$onDenoised = $on.statistics.denoisedRadiance
$offMean = [double]$offDenoised.temporalMeanLuminance
$onMean = [double]$onDenoised.temporalMeanLuminance
$meanDenominator = [Math]::Max([Math]::Abs($offMean), 1.0e-6)
$offVariance = [double]$offDenoised.temporalVariance
$onVariance = [double]$onDenoised.temporalVariance

$pairedReport = [ordered]@{
    schemaVersion = 2
    comparison = if ($CompareSpatiotemporalPolicy)
    {
        "fixed-spatial-filter-vs-bounded-spatiotemporal-policy"
    }
    else
    {
        "surface-variance-filter-off-on"
    }
    signalDomain = "linear-hdr"
    reference = "none"
    currentEstimatorMeanBaseline = [ordered]@{
        source = "arithmetic mean of per-frame ReflectionEvaluatedRadiance"
        physicalReference = $false
        sampleCount = $MeasurementFrames
        filterOff = $off.currentEstimatorMeanBaseline
        filterOn = $on.currentEstimatorMeanBaseline
        meanAbsoluteDifference = [Math]::Abs(
            [double]$on.currentEstimatorMeanBaseline.meanLuminance -
            [double]$off.currentEstimatorMeanBaseline.meanLuminance)
    }
    pairedConditions = [ordered]@{
        separateProcessReset = $true
        cameraAndAnimationFixed = $true
        scene = $Scene
        cameraDistanceScale = $CameraDistanceScale
        warmupFrames = $WarmupFrames
        measurementFrames = $MeasurementFrames
        sampleAndTemporalIndexSequencesMatch = $sampleSequenceMatches
        temporalWeight = $TemporalWeight
        stochasticSamplingEnabled = $true
        onlyIntentionalDifference = if ($CompareSpatiotemporalPolicy)
        {
            "spatiotemporalSpatialPolicyEnabled"
        }
        else
        {
            "surfaceVarianceFilterEnabled"
        }
        variantA = if ($CompareSpatiotemporalPolicy) { "fixed-spatial-filter" } else { "filter-off" }
        variantB = if ($CompareSpatiotemporalPolicy) { "bounded-spatiotemporal-policy" } else { "filter-on" }
    }
    roi = $off.roi
    filterOffReport = [System.IO.Path]::GetFileName($offPath)
    filterOnReport = [System.IO.Path]::GetFileName($onPath)
    result = [ordered]@{
        filterOffResolvedControl = $offResolved
        filterOnResolvedControl = $onResolved
        filterOffDenoised = $offDenoised
        filterOnDenoised = $onDenoised
        denoisedVarianceReductionPercent = if ($offVariance -gt 0.0)
        {
            100.0 * (1.0 - $onVariance / $offVariance)
        }
        else
        {
            $null
        }
        denoisedVarianceChangeBRelativeToAPercent = if ($offVariance -gt 0.0)
        {
            100.0 * ($onVariance / $offVariance - 1.0)
        }
        else
        {
            $null
        }
        denoisedMeasurementMeanAbsoluteDifference = [Math]::Abs($onMean - $offMean)
        denoisedMeasurementMeanRelativeDifference = [Math]::Abs($onMean - $offMean) / $meanDenominator
        resolvedControlVarianceRelativeDifference = if ([double]$offResolved.temporalVariance -gt 0.0)
        {
            [Math]::Abs(
                [double]$onResolved.temporalVariance - [double]$offResolved.temporalVariance) /
                [double]$offResolved.temporalVariance
        }
        else
        {
            $null
        }
        evaluatedMeanAbsoluteDifference = [Math]::Abs(
            [double]$on.statistics.evaluatedRadiance.temporalMeanLuminance -
            [double]$off.statistics.evaluatedRadiance.temporalMeanLuminance)
        filterOffResolvedRmseToCurrentEstimatorMean = [double]$off.currentEstimatorMeanBaseline.resolvedRmse
        filterOnResolvedRmseToCurrentEstimatorMean = [double]$on.currentEstimatorMeanBaseline.resolvedRmse
        resolvedRmseChangePercent = if ([double]$off.currentEstimatorMeanBaseline.resolvedRmse -gt 0.0)
        {
            100.0 * (
                [double]$on.currentEstimatorMeanBaseline.resolvedRmse /
                [double]$off.currentEstimatorMeanBaseline.resolvedRmse - 1.0)
        }
        else
        {
            $null
        }
    }
    limitations = @(
        "The comparison is relative to the current approximate estimator, not a physical ground truth.",
        "A matching sample-index sequence establishes paired scheduling but does not establish estimator correctness.",
        "The 64-frame level is a development gate; 256 frames remain the standard PR evaluation."
    )
}

$json = $pairedReport | ConvertTo-Json -Depth 12
[System.IO.File]::WriteAllText($output, $json + "`r`n", [System.Text.UTF8Encoding]::new($false))

[PSCustomObject]@{
    sampleSequenceMatches = $sampleSequenceMatches
    varianceChangeBRelativeToAPercent = $pairedReport.result.denoisedVarianceChangeBRelativeToAPercent
    meanRelativeDifference = $pairedReport.result.denoisedMeasurementMeanRelativeDifference
    report = $output
} | Format-List

if (-not $sampleSequenceMatches)
{
    throw "Filter off/on sample or temporal index sequences do not match."
}
