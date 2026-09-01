param(
    [string]$Executable = ".\bin\x64\Debug\RtPbrSurvey.exe",
    [string]$OutputDirectory = ".\Tests\DlssRayReconstruction\captures-temporal",
    [int]$SampleStride = 16,
    [switch]$AnalyzeOnly
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Get-CapturePath([string]$CaseId, [string]$Variant)
{
    return Join-Path $outputPath "$CaseId-$Variant.png"
}

function Invoke-CaptureVariant([string]$Variant, [bool]$NativeEnabled)
{
    $logPath = Join-Path $outputPath "$Variant.log"
    $arguments = @(
        "-AutoSelectGltfDamagedHelmet",
        "-EnableDlssRayReconstruction",
        "-ReflectionCapturePlan", $planPath,
        "-ReflectionCaptureVariant", $Variant,
        "-ExitAfterCapture",
        "-LogToFile", $logPath)
    if ($NativeEnabled)
    {
        $arguments += "-EnableExperimentalNativeRayReconstruction"
    }

    Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
    foreach ($capture in $plan.captures)
    {
        Remove-Item -LiteralPath (Get-CapturePath $capture.caseId $Variant) -Force -ErrorAction SilentlyContinue
    }

    $process = Start-Process -FilePath $executablePath -ArgumentList $arguments `
        -WorkingDirectory $repositoryRoot -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0)
    {
        throw "$Variant capture exited with code $($process.ExitCode)."
    }
    foreach ($capture in $plan.captures)
    {
        $capturePath = Get-CapturePath $capture.caseId $Variant
        if (!(Test-Path -LiteralPath $capturePath) -or (Get-Item -LiteralPath $capturePath).Length -le 0)
        {
            throw "$Variant did not produce $capturePath."
        }
    }
    return Read-CaptureVariant $Variant $NativeEnabled
}

function Read-CaptureVariant([string]$Variant, [bool]$NativeEnabled)
{
    $logPath = Join-Path $outputPath "$Variant.log"
    if (!(Test-Path -LiteralPath $logPath))
    {
        throw "Missing log for $Variant."
    }
    $logLines = @(Get-Content -LiteralPath $logPath)
    $rrLine = $logLines | Where-Object { $_.StartsWith("[RR]") } | Select-Object -Last 1
    $errorCount = @($logLines | Where-Object { $_.StartsWith("[ERROR]") }).Count
    if (!$rrLine -or $errorCount -ne 0)
    {
        throw "$Variant has a missing RR diagnostic or D3D12 errors."
    }
    if ($NativeEnabled)
    {
        if ($rrLine -notmatch "lastEvaluateResult=Result::eOk" -or
            $rrLine -notmatch "lastEvaluateOutput=native-output")
        {
            throw "Native variant did not produce native RR output."
        }
    }
    elseif ($rrLine -notmatch "lastEvaluateOutput=fallback")
    {
        throw "Fallback variant did not use copy fallback."
    }

    return [pscustomobject][ordered]@{
        name = $Variant
        nativeEnabled = $NativeEnabled
        logPath = $logPath
        rrDiagnostic = [string]$rrLine
        errorCount = $errorCount
        warningCount = @($logLines | Where-Object { $_.StartsWith("[WARNING]") }).Count
    }
}

function Measure-ImageDifference([string]$FirstPath, [string]$SecondPath)
{
    $first = [System.Drawing.Bitmap]::FromFile($FirstPath)
    $second = [System.Drawing.Bitmap]::FromFile($SecondPath)
    try
    {
        if ($first.Width -ne $second.Width -or $first.Height -ne $second.Height)
        {
            throw "Capture dimensions differ."
        }
        [long]$samples = 0
        [long]$changed = 0
        [double]$absoluteError = 0
        [double]$squaredError = 0
        [double]$maximumError = 0
        for ($y = 0; $y -lt $first.Height; $y += $SampleStride)
        {
            for ($x = 0; $x -lt $first.Width; $x += $SampleStride)
            {
                $a = $first.GetPixel($x, $y)
                $b = $second.GetPixel($x, $y)
                $differences = @(
                    ([Math]::Abs($a.R - $b.R) / 255.0)
                    ([Math]::Abs($a.G - $b.G) / 255.0)
                    ([Math]::Abs($a.B - $b.B) / 255.0))
                $pixelMaximum = [Math]::Max($differences[0], [Math]::Max($differences[1], $differences[2]))
                if ($pixelMaximum -gt 0)
                {
                    $changed++
                }
                foreach ($difference in $differences)
                {
                    $absoluteError += $difference
                    $squaredError += $difference * $difference
                }
                $maximumError = [Math]::Max($maximumError, $pixelMaximum)
                $samples++
            }
        }
        return [pscustomobject][ordered]@{
            sampleCount = $samples
            changedPixelRatio = $changed / [double]$samples
            meanAbsoluteError = $absoluteError / ($samples * 3.0)
            rootMeanSquareError = [Math]::Sqrt($squaredError / ($samples * 3.0))
            maximumAbsoluteError = $maximumError
        }
    }
    finally
    {
        $first.Dispose()
        $second.Dispose()
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$executablePath = (Resolve-Path (Join-Path $repositoryRoot $Executable)).Path
$planPath = (Resolve-Path (Join-Path $PSScriptRoot "capture-plan-temporal.json")).Path
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
$plan = Get-Content -LiteralPath $planPath -Raw -Encoding UTF8 | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$testedCommit = (& git.exe -C $repositoryRoot rev-parse HEAD).Trim()

if ($AnalyzeOnly)
{
    $fallbackResult = Read-CaptureVariant "fallback" $false
    $nativeResult = Read-CaptureVariant "native" $true
}
else
{
    $fallbackResult = Invoke-CaptureVariant "fallback" $false
    Start-Sleep -Seconds 15
    $nativeResult = Invoke-CaptureVariant "native" $true
}

$frameComparisons = @()
foreach ($capture in $plan.captures)
{
    $frameComparisons += [pscustomobject][ordered]@{
        caseId = $capture.caseId
        frame = $capture.frame
        fallbackSha256 = (Get-FileHash -LiteralPath (Get-CapturePath $capture.caseId "fallback") -Algorithm SHA256).Hash
        nativeSha256 = (Get-FileHash -LiteralPath (Get-CapturePath $capture.caseId "native") -Algorithm SHA256).Hash
        metrics = Measure-ImageDifference `
            (Get-CapturePath $capture.caseId "fallback") `
            (Get-CapturePath $capture.caseId "native")
    }
}

$settlingComparisons = @()
foreach ($variant in @("fallback", "native"))
{
    foreach ($caseId in @("settle-1", "settle-6"))
    {
        $settlingComparisons += [pscustomobject][ordered]@{
            variant = $variant
            from = $caseId
            to = "settle-15"
            metrics = Measure-ImageDifference `
                (Get-CapturePath $caseId $variant) `
                (Get-CapturePath "settle-15" $variant)
        }
    }
}

$report = [pscustomobject][ordered]@{
    schemaVersion = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    testedCommit = $testedCommit
    sampleStride = $SampleStride
    capturePlan = "capture-plan-temporal.json"
    capturePlanSha256 = (Get-FileHash -LiteralPath $planPath -Algorithm SHA256).Hash
    fallback = $fallbackResult
    native = $nativeResult
    frameComparisons = $frameComparisons
    settlingComparisons = $settlingComparisons
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath "report.json") -Encoding utf8

$rows = foreach ($comparison in $frameComparisons)
{
    "| $($comparison.caseId) | $($comparison.frame) | $($comparison.metrics.changedPixelRatio) | $($comparison.metrics.meanAbsoluteError) | $($comparison.metrics.rootMeanSquareError) |"
}
$settlingRows = foreach ($comparison in $settlingComparisons)
{
    "| $($comparison.variant) | $($comparison.from) | $($comparison.metrics.changedPixelRatio) | $($comparison.metrics.meanAbsoluteError) | $($comparison.metrics.rootMeanSquareError) |"
}
@"
# DLSS Ray Reconstruction Temporal A/B Report

- Tested commit: ``$testedCommit``
- Sample stride: $SampleStride
- Fallback diagnostic: ``$($fallbackResult.rrDiagnostic)``
- Native diagnostic: ``$($nativeResult.rrDiagnostic)``
- D3D12 errors: fallback $($fallbackResult.errorCount), native $($nativeResult.errorCount)

## Fallback / Native Per Frame

| Case | Frame | Changed ratio | MAE | RMSE |
|---|---:|---:|---:|---:|
$($rows -join "`r`n")

## Settling Relative To Frame 15

| Variant | Earlier capture | Changed ratio | MAE | RMSE |
|---|---|---:|---:|---:|
$($settlingRows -join "`r`n")

These metrics establish deterministic temporal behavior and visible differences only. They do not establish physical correctness or production image quality.
"@ | Set-Content -LiteralPath (Join-Path $outputPath "report.md") -Encoding utf8

$frameComparisons | Format-Table caseId, frame, @{Label="MAE"; Expression={$_.metrics.meanAbsoluteError}}, @{Label="RMSE"; Expression={$_.metrics.rootMeanSquareError}}
