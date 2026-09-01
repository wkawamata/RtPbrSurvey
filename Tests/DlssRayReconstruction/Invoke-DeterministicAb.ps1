param(
    [string]$Executable = ".\bin\x64\Debug\RtPbrSurvey.exe",
    [string]$OutputDirectory = ".\Screenshots\DlssRayReconstructionAb",
    [int]$WarmupFrames = 60,
    [int]$SampleStride = 16,
    [switch]$AnalyzeOnly
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Invoke-RrCapture([string]$Name, [bool]$NativeEnabled)
{
    $capturePath = Join-Path $outputPath "$Name.png"
    $logPath = Join-Path $outputPath "$Name.log"
    $arguments = @(
        "-AutoSelectGltfDamagedHelmet",
        "-EnableDlssRayReconstruction",
        "-CaptureReflectionResolvedRadiance",
        "-CapturePath", $capturePath,
        "-CaptureAfterFrames", $WarmupFrames,
        "-ExitAfterCapture",
        "-LogToFile", $logPath)
    if ($NativeEnabled)
    {
        $arguments += "-EnableExperimentalNativeRayReconstruction"
    }

    $process = $null
    for ($attempt = 1; $attempt -le 3; ++$attempt)
    {
        Remove-Item -LiteralPath $capturePath, $logPath -Force -ErrorAction SilentlyContinue
        $process = Start-Process -FilePath $executablePath -ArgumentList $arguments `
            -WorkingDirectory $repositoryRoot -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -eq 0 -and (Test-Path -LiteralPath $capturePath) -and
            (Test-Path -LiteralPath $logPath) -and (Get-Item -LiteralPath $logPath).Length -gt 0)
        {
            break
        }
        if ($attempt -eq 3)
        {
            throw "$Name did not produce capture and log artifacts after $attempt attempts."
        }
        Start-Sleep -Seconds 30
    }

    $logLines = @(Get-Content -LiteralPath $logPath)
    $rrLine = $logLines | Where-Object { $_.StartsWith("[RR]") } | Select-Object -Last 1
    $errorCount = @($logLines | Where-Object { $_.StartsWith("[ERROR]") }).Count
    $warningCount = @($logLines | Where-Object { $_.StartsWith("[WARNING]") }).Count
    if (!$rrLine -or $errorCount -ne 0)
    {
        throw "$Name has a missing RR diagnostic or D3D12 errors."
    }

    return [pscustomobject][ordered]@{
        name = $Name
        nativeEnabled = $NativeEnabled
        exitCode = $process.ExitCode
        capturePath = $capturePath
        captureSha256 = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash
        logPath = $logPath
        rrDiagnostic = [string]$rrLine
        errorCount = $errorCount
        warningCount = $warningCount
    }
}

function Measure-ImageDifference([string]$FallbackPath, [string]$NativePath)
{
    $fallback = [System.Drawing.Bitmap]::FromFile($FallbackPath)
    $native = [System.Drawing.Bitmap]::FromFile($NativePath)
    try
    {
        if ($fallback.Width -ne $native.Width -or $fallback.Height -ne $native.Height)
        {
            throw "Capture dimensions differ."
        }

        [long]$samples = 0
        [long]$changed = 0
        [long]$fallbackNonBlack = 0
        [long]$nativeNonBlack = 0
        [double]$absoluteError = 0
        [double]$squaredError = 0
        [double]$maximumError = 0
        [double]$fallbackLuminance = 0
        [double]$nativeLuminance = 0
        for ($y = 0; $y -lt $fallback.Height; $y += $SampleStride)
        {
            for ($x = 0; $x -lt $fallback.Width; $x += $SampleStride)
            {
                $a = $fallback.GetPixel($x, $y)
                $b = $native.GetPixel($x, $y)
                $differences = @(
                    ([Math]::Abs($a.R - $b.R) / 255.0)
                    ([Math]::Abs($a.G - $b.G) / 255.0)
                    ([Math]::Abs($a.B - $b.B) / 255.0))
                $pixelMaximumError = [Math]::Max($differences[0], [Math]::Max($differences[1], $differences[2]))
                if ($pixelMaximumError -gt 0)
                {
                    $changed++
                }
                foreach ($difference in $differences)
                {
                    $absoluteError += $difference
                    $squaredError += $difference * $difference
                }
                $maximumError = [Math]::Max($maximumError, $pixelMaximumError)
                $fallbackNonBlack += [int](($a.R -ne 0) -or ($a.G -ne 0) -or ($a.B -ne 0))
                $nativeNonBlack += [int](($b.R -ne 0) -or ($b.G -ne 0) -or ($b.B -ne 0))
                $fallbackLuminance += (0.2126 * $a.R + 0.7152 * $a.G + 0.0722 * $a.B) / 255.0
                $nativeLuminance += (0.2126 * $b.R + 0.7152 * $b.G + 0.0722 * $b.B) / 255.0
                $samples++
            }
        }

        return [pscustomobject][ordered]@{
            width = $fallback.Width
            height = $fallback.Height
            sampleStride = $SampleStride
            sampleCount = $samples
            changedPixelRatio = $changed / [double]$samples
            meanAbsoluteError = $absoluteError / ($samples * 3.0)
            rootMeanSquareError = [Math]::Sqrt($squaredError / ($samples * 3.0))
            maximumAbsoluteError = $maximumError
            fallbackMeanLuminance = $fallbackLuminance / $samples
            nativeMeanLuminance = $nativeLuminance / $samples
            meanLuminanceDelta = ($nativeLuminance - $fallbackLuminance) / $samples
            fallbackNonBlackPixelRatio = $fallbackNonBlack / [double]$samples
            nativeNonBlackPixelRatio = $nativeNonBlack / [double]$samples
        }
    }
    finally
    {
        $fallback.Dispose()
        $native.Dispose()
    }
}

function Read-RrCapture([string]$Name, [bool]$NativeEnabled)
{
    $capturePath = Join-Path $outputPath "$Name.png"
    $logPath = Join-Path $outputPath "$Name.log"
    if (!(Test-Path -LiteralPath $capturePath) -or !(Test-Path -LiteralPath $logPath))
    {
        throw "Missing artifacts for $Name."
    }
    $logLines = @(Get-Content -LiteralPath $logPath)
    $rrLine = $logLines | Where-Object { $_.StartsWith("[RR]") } | Select-Object -Last 1
    return [pscustomobject][ordered]@{
        name = $Name
        nativeEnabled = $NativeEnabled
        exitCode = 0
        capturePath = $capturePath
        captureSha256 = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash
        logPath = $logPath
        rrDiagnostic = [string]$rrLine
        errorCount = @($logLines | Where-Object { $_.StartsWith("[ERROR]") }).Count
        warningCount = @($logLines | Where-Object { $_.StartsWith("[WARNING]") }).Count
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$executablePath = (Resolve-Path (Join-Path $repositoryRoot $Executable)).Path
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$testedCommit = (& git.exe -C $repositoryRoot rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or !$testedCommit)
{
    throw "Failed to resolve the tested commit."
}
$testedCommit = $testedCommit.Trim()

if ($AnalyzeOnly)
{
    Write-Host "Reading existing captures."
    $nativeResult = Read-RrCapture "native" $true
    $fallbackResult = Read-RrCapture "fallback" $false
}
else
{
    $nativeResult = Invoke-RrCapture "native" $true
    Start-Sleep -Seconds 15
    $fallbackResult = Invoke-RrCapture "fallback" $false
}
if ($fallbackResult.rrDiagnostic -notmatch "lastEvaluateOutput=fallback")
{
    throw "Variant A did not use copy fallback."
}
if ($nativeResult.rrDiagnostic -notmatch "lastEvaluateResult=Result::eOk" -or
    $nativeResult.rrDiagnostic -notmatch "lastEvaluateOutput=native-output")
{
    throw "Variant B did not produce native output."
}

Write-Host "Measuring image difference with stride $SampleStride."
$metrics = Measure-ImageDifference $fallbackResult.capturePath $nativeResult.capturePath
Write-Host "Writing reports."
$report = [pscustomobject][ordered]@{
    schemaVersion = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    testedCommit = $testedCommit
    warmupFrames = $WarmupFrames
    executionOrder = @("native", "fallback")
    onlyIntentionalDifference = "experimental native ray reconstruction enable"
    fallback = $fallbackResult
    native = $nativeResult
    metrics = $metrics
}
$reportJson = $report | ConvertTo-Json -Depth 8
Write-Host "JSON report serialized."
$reportJson | Set-Content -LiteralPath (Join-Path $outputPath "report.json") -Encoding utf8
Write-Host "JSON report written."
@"
# DLSS Ray Reconstruction Deterministic A/B Report

- Tested commit: ``$($report.testedCommit)``
- Warm-up frames: $WarmupFrames
- Fallback diagnostic: ``$($fallbackResult.rrDiagnostic)``
- Native diagnostic: ``$($nativeResult.rrDiagnostic)``
- D3D12 errors: fallback $($fallbackResult.errorCount), native $($nativeResult.errorCount)
- D3D12 warnings: fallback $($fallbackResult.warningCount), native $($nativeResult.warningCount)

| Metric | Value |
|---|---:|
| Resolution | $($metrics.width) x $($metrics.height) |
| Changed pixel ratio | $($metrics.changedPixelRatio) |
| Mean absolute error | $($metrics.meanAbsoluteError) |
| RMSE | $($metrics.rootMeanSquareError) |
| Maximum absolute error | $($metrics.maximumAbsoluteError) |
| Fallback mean luminance | $($metrics.fallbackMeanLuminance) |
| Native mean luminance | $($metrics.nativeMeanLuminance) |
| Mean luminance delta | $($metrics.meanLuminanceDelta) |
| Fallback non-black ratio | $($metrics.fallbackNonBlackPixelRatio) |
| Native non-black ratio | $($metrics.nativeNonBlackPixelRatio) |

Generated captures, logs, and reports must not be committed.
"@ | Set-Content -LiteralPath (Join-Path $outputPath "report.md") -Encoding utf8

$metrics | Format-List
Write-Host "Done."
