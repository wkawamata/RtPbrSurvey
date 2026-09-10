param(
    [string]$Executable = ".\bin\x64\Debug\RtPbrSurvey.exe",
    [string]$OutputDirectory = ".\Tests\DlssRayReconstruction\captures-input-contract",
    [int]$CaptureAfterFrames = 60,
    [float]$OrbitDegrees = 20.0,
    [int]$OrbitFrames = 30,
    [int]$SampleStride = 16,
    [switch]$AnalyzeOnly
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$views = @(
    [pscustomobject]@{ name = "evaluated-radiance"; contract = "RR noisy input color" },
    [pscustomobject]@{ name = "albedo"; contract = "RR albedo input" },
    [pscustomobject]@{ name = "specular-albedo"; contract = "RR specular-albedo input" },
    [pscustomobject]@{ name = "normal"; contract = "RR normal input" },
    [pscustomobject]@{ name = "roughness"; contract = "RR roughness input" },
    [pscustomobject]@{ name = "specular-hit-distance"; contract = "RR specular-hit-distance input" },
    [pscustomobject]@{ name = "motion-vector"; contract = "RR motion-vector input" },
    [pscustomobject]@{ name = "depth"; contract = "RR depth input" })

function Measure-Capture([string]$Path)
{
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try
    {
        [long]$samples = 0
        [long]$nonBlack = 0
        [double]$red = 0
        [double]$green = 0
        [double]$blue = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += $SampleStride)
        {
            for ($x = 0; $x -lt $bitmap.Width; $x += $SampleStride)
            {
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.R -ne 0 -or $pixel.G -ne 0 -or $pixel.B -ne 0)
                {
                    $nonBlack++
                }
                $red += $pixel.R / 255.0
                $green += $pixel.G / 255.0
                $blue += $pixel.B / 255.0
                $samples++
            }
        }
        return [pscustomobject][ordered]@{
            width = $bitmap.Width
            height = $bitmap.Height
            sampleCount = $samples
            nonBlackPixelRatio = $nonBlack / [double]$samples
            meanRgb = @(
                ($red / $samples)
                ($green / $samples)
                ($blue / $samples))
        }
    }
    finally
    {
        $bitmap.Dispose()
    }
}

function Invoke-InputCapture([object]$View)
{
    $capturePath = Join-Path $outputPath "$($View.name).png"
    $logPath = Join-Path $outputPath "$($View.name).log"
    Remove-Item -LiteralPath $capturePath, $logPath -Force -ErrorAction SilentlyContinue
    $arguments = @(
        "-AutoSelectGltfDamagedHelmet",
        "-EnableDlssRayReconstruction",
        "-CaptureReflectionResolvedRadiance",
        "-ReflectionCaptureDebugView", $View.name,
        "-CapturePath", $capturePath,
        "-CaptureAfterFrames", $CaptureAfterFrames,
        "-ReflectionOrbitDegrees", $OrbitDegrees.ToString([Globalization.CultureInfo]::InvariantCulture),
        "-ReflectionOrbitFrames", $OrbitFrames,
        "-ExitAfterCapture",
        "-LogToFile", $logPath)
    $process = Start-Process -FilePath $executablePath -ArgumentList $arguments `
        -WorkingDirectory $repositoryRoot -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $capturePath))
    {
        throw "$($View.name) capture failed with exit code $($process.ExitCode)."
    }
}

function Read-InputCapture([object]$View)
{
    $capturePath = Join-Path $outputPath "$($View.name).png"
    $logPath = Join-Path $outputPath "$($View.name).log"
    if (!(Test-Path -LiteralPath $capturePath) -or !(Test-Path -LiteralPath $logPath))
    {
        throw "Missing artifacts for $($View.name)."
    }
    $logLines = @(Get-Content -LiteralPath $logPath)
    $errorCount = @($logLines | Where-Object { $_.StartsWith("[ERROR]") }).Count
    $rrLine = $logLines | Where-Object { $_.StartsWith("[RR]") } | Select-Object -Last 1
    if ($errorCount -ne 0 -or !$rrLine)
    {
        throw "$($View.name) has a missing RR diagnostic or D3D12 errors."
    }
    $metrics = Measure-Capture $capturePath
    if ($metrics.width -le 0 -or $metrics.height -le 0 -or $metrics.nonBlackPixelRatio -le 0)
    {
        throw "$($View.name) capture is empty or black."
    }
    return [pscustomobject][ordered]@{
        name = $View.name
        contract = $View.contract
        capturePath = $capturePath
        captureSha256 = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash
        rrDiagnostic = [string]$rrLine
        errorCount = $errorCount
        warningCount = @($logLines | Where-Object { $_.StartsWith("[WARNING]") }).Count
        metrics = $metrics
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$executablePath = (Resolve-Path (Join-Path $repositoryRoot $Executable)).Path
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

if (!$AnalyzeOnly)
{
    foreach ($view in $views)
    {
        Invoke-InputCapture $view
    }
}

$captures = @($views | ForEach-Object { Read-InputCapture $_ })
if (@($captures.captureSha256 | Select-Object -Unique).Count -ne $captures.Count)
{
    throw "Input debug captures must not be byte-identical."
}

$report = [pscustomobject][ordered]@{
    schemaVersion = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    testedCommit = (& git.exe -C $repositoryRoot rev-parse HEAD).Trim()
    scene = "DamagedHelmet"
    captureAfterFrames = $CaptureAfterFrames
    orbitDegrees = $OrbitDegrees
    orbitFrames = $OrbitFrames
    sampleStride = $SampleStride
    captures = $captures
    interpretation = "Displayed debug views validate availability and obvious corruption, not raw-value sign or coordinate-space correctness."
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath "report.json") -Encoding utf8

$rows = foreach ($capture in $captures)
{
    "| $($capture.name) | $($capture.contract) | $($capture.metrics.width) x $($capture.metrics.height) | $($capture.metrics.nonBlackPixelRatio) | $($capture.captureSha256) |"
}
@"
# DLSS Ray Reconstruction Input Contract Capture

- Tested commit: ``$($report.testedCommit)``
- Scene: $($report.scene)
- Capture frame: $CaptureAfterFrames
- D3D12 errors: $(@($captures | Measure-Object -Property errorCount -Sum).Sum)

| View | Contract | Resolution | Non-black ratio | SHA-256 |
|---|---|---:|---:|---|
$($rows -join "`r`n")

These displayed debug views validate input availability and obvious corruption. They do not by themselves prove raw motion-vector sign/scale, depth convention, or normal coordinate-space correctness.
"@ | Set-Content -LiteralPath (Join-Path $outputPath "report.md") -Encoding utf8

$captures | Format-Table name, contract, @{Label="NonBlack"; Expression={$_.metrics.nonBlackPixelRatio}}, warningCount
