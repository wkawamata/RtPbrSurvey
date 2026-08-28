param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-NearlyEqual($Actual, $Expected, [double]$Tolerance = 1.0e-9)
{
    if ($null -eq $Actual -or $null -eq $Expected)
    {
        return $null -eq $Actual -and $null -eq $Expected
    }
    return [Math]::Abs([double]$Actual - [double]$Expected) -le $Tolerance
}

function Find-SettlingFrame(
    [array]$Frames,
    [double]$SettledMean,
    [double]$InitialError,
    [double]$Fraction,
    [int]$RequiredSamples,
    [uint64]$StopFrame)
{
    $threshold = $InitialError * $Fraction
    for ($index = 0; $index + $RequiredSamples -le $Frames.Count; ++$index)
    {
        $withinThreshold = $true
        for ($consecutive = 0; $consecutive -lt $RequiredSamples; ++$consecutive)
        {
            $value = [double]$Frames[$index + $consecutive].resolvedMeanLuminance
            $withinThreshold = $withinThreshold -and [Math]::Abs($value - $SettledMean) -le $threshold
        }
        if ($withinThreshold)
        {
            return [uint64]$Frames[$index].automationFrameIndex - $StopFrame
        }
    }
    return $null
}

$report = Get-Content -LiteralPath $ReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$failures = [System.Collections.Generic.List[string]]::new()
if ($report.schemaVersion -ne 12)
{
    $failures.Add("schemaVersion must be 12")
}

$frames = @($report.frames)
$motionFrames = [int]$report.cameraMotionTimeline.framesPerDirection
for ($index = 0; $index -lt $frames.Count; ++$index)
{
    $expectedPhase = if ($index -lt $motionFrames) { "forward" } elseif ($index -lt $motionFrames * 2) { "reverse" } else { "stationary" }
    if ($report.cameraMotionTimeline.enabled -and $frames[$index].cameraMotionPhase -ne $expectedPhase)
    {
        $failures.Add("frame $index cameraMotionPhase does not match the declared timeline")
        break
    }
    if ($index -gt 0 -and [uint64]$frames[$index].automationFrameIndex -ne [uint64]$frames[$index - 1].automationFrameIndex + 1)
    {
        $failures.Add("automationFrameIndex must be contiguous")
        break
    }

    $rateSum = [double]$frames[$index].noHistoryRate + [double]$frames[$index].outsideHistoryRate +
        [double]$frames[$index].depthRejectRate + [double]$frames[$index].normalRejectRate +
        [double]$frames[$index].temporalAcceptanceRate
    if ([Math]::Abs($rateSum - 1.0) -gt 2.0e-6)
    {
        $failures.Add("frame $index temporal status rates sum to $rateSum")
        break
    }
}

$moving = @($frames | Where-Object { $_.cameraMotionPhase -ne "stationary" })
$stationary = @($frames | Where-Object { $_.cameraMotionPhase -eq "stationary" })
if ($report.cameraMotionTimeline.enabled -and $moving.Count -gt 0 -and
    ($moving | Measure-Object maximumMotionVectorMagnitudeNdc -Maximum).Maximum -le 0.0)
{
    $failures.Add("moving timeline contains no nonzero motion vector")
}
if ($stationary.Count -gt 0 -and
    ($stationary | Measure-Object maximumMotionVectorMagnitudeNdc -Maximum).Maximum -gt 1.0e-7)
{
    $failures.Add("stationary timeline contains a nonzero motion vector")
}

$settling = $report.settlingDiagnostic
if ($settling.enabled -and $stationary.Count -gt 0)
{
    $windowCount = [Math]::Min(8, $stationary.Count)
    $settledMean = [double](
        $stationary[($stationary.Count - $windowCount)..($stationary.Count - 1)] |
            Measure-Object resolvedMeanLuminance -Average).Average
    $initialMean = [double]$stationary[0].resolvedMeanLuminance
    $initialError = [Math]::Abs($initialMean - $settledMean)
    $minimumError = [Math]::Max([Math]::Abs($settledMean) * 0.01, 1.0e-6)
    $requiredSamples = [int]$settling.requiredConsecutiveSamples
    $stopFrame = [uint64]$settling.stopAutomationFrame
    $valid = $initialError -gt $minimumError -and $stationary.Count -ge $requiredSamples
    if ([bool]$settling.valid -ne $valid)
    {
        $failures.Add("settling valid flag does not match the recomputed threshold")
    }

    $expectedSettling = @{
        t50Frames = if ($valid) { Find-SettlingFrame $stationary $settledMean $initialError 0.5 $requiredSamples $stopFrame } else { $null }
        t90Frames = if ($valid) { Find-SettlingFrame $stationary $settledMean $initialError 0.1 $requiredSamples $stopFrame } else { $null }
        t95Frames = if ($valid) { Find-SettlingFrame $stationary $settledMean $initialError 0.05 $requiredSamples $stopFrame } else { $null }
    }
    foreach ($field in @("t50Frames", "t90Frames", "t95Frames"))
    {
        if (-not (Test-NearlyEqual $settling.$field $expectedSettling[$field]))
        {
            $failures.Add("$field does not match recomputed value $($expectedSettling[$field])")
        }
    }
}

$summary = [ordered]@{
    schemaVersion = $report.schemaVersion
    scene = $report.scene
    measurementFrames = $frames.Count
    movingSamples = $moving.Count
    stationarySamples = $stationary.Count
    settlingValid = $settling.valid
    t50Frames = $settling.t50Frames
    t90Frames = $settling.t90Frames
    t95Frames = $settling.t95Frames
    status = if ($failures.Count -eq 0) { "pass" } else { "fail" }
    failures = @($failures)
}
$summary | ConvertTo-Json -Depth 3
if ($failures.Count -ne 0)
{
    exit 1
}
