[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [ValidateSet('Point', 'Spot')]
    [string]$LightType = 'Point',
    [ValidateRange(1, 4096)]
    [int]$Samples = 1,
    [uint32]$Seed = 1,
    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exe = Join-Path $root 'bin/x64/Debug/RtPbrSurvey.exe'
$sourceScene = Join-Path $root 'Assets/Scenes/MultiLightValidation/scene.json'
$sourcePreset = Join-Path $root 'Assets/Scenes/MultiLightValidation/render-preset.json'
if (-not $OutputDirectory)
{
    $OutputDirectory = Join-Path $root "bin/x64/Debug/PathTracing${LightType}Visibility"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

function Read-Pfm([string]$Path)
{
    $stream = [IO.File]::OpenRead($Path)
    try
    {
        $reader = [IO.BinaryReader]::new($stream)
        $lines = @()
        for ($lineIndex = 0; $lineIndex -lt 3; ++$lineIndex)
        {
            $bytes = [System.Collections.Generic.List[byte]]::new()
            while ($true)
            {
                $value = $reader.ReadByte()
                if ($value -eq 10) { break }
                if ($value -ne 13) { $bytes.Add($value) }
            }
            $lines += [Text.Encoding]::ASCII.GetString($bytes.ToArray())
        }
        if ($lines[0] -ne 'PF' -or $lines[2] -ne '-1.0')
        {
            throw "Unexpected PFM header: $Path"
        }
        $dimensions = $lines[1] -split ' '
        $width = [int]$dimensions[0]
        $height = [int]$dimensions[1]
        if ($width -le 0 -or $height -le 0 -or [long]$width * $height * 3 -gt [int]::MaxValue / 4)
        {
            throw "Invalid PFM dimensions: $Path"
        }
        $count = $width * $height * 3
        $bytes = $reader.ReadBytes($count * 4)
        if ($bytes.Length -ne $count * 4 -or $stream.Position -ne $stream.Length)
        {
            throw "Invalid PFM byte count: $Path"
        }
        $values = [float[]]::new($count)
        [Buffer]::BlockCopy($bytes, 0, $values, 0, $bytes.Length)
        return @{ Width = $width; Height = $height; Values = $values }
    }
    finally
    {
        $stream.Dispose()
    }
}

$variants = [ordered]@{
    clear = $null
    between = @(-1, 1.5, 0)
    beyond = @(-3, 4.5, 0)
}
$captures = [ordered]@{}
foreach ($variant in $variants.Keys)
{
    $scene = Get-Content -LiteralPath $sourceScene -Raw | ConvertFrom-Json -AsHashtable
    $blocker = @($scene.nodes | Where-Object { $_.id -eq 'blocker' })[0]
    $blocker.primitive.size = 0.45
    $blocker.translation = $variants[$variant]
    foreach ($node in $scene.nodes)
    {
        if ($node.id -in @('smooth', 'rough', 'diffuse'))
        {
            $node.translation = @(100, 100, 100)
        }
    }
    if ($variant -eq 'clear')
    {
        $scene.nodes = @($scene.nodes | Where-Object { $_.id -ne 'blocker' })
    }
    $scene.camera.position = @(0, 4, -7)
    $scene.camera.target = @(0, 0, 0)
    $scene.renderPreset = "$variant-preset.json"
    $scenePath = Join-Path $OutputDirectory "$variant-scene.json"
    [IO.File]::WriteAllText($scenePath, ($scene | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))

    $preset = Get-Content -LiteralPath $sourcePreset -Raw | ConvertFrom-Json -AsHashtable
    $light = $preset.lighting.lights[1]
    $light.position = @(-2, 3, 0)
    if ($LightType -eq 'Spot')
    {
        $light.type = 'spot'
        $light.direction = @(0.5547002, -0.8320503, 0)
    }
    $preset.lighting.lights = @($light)
    $preset.lighting.primaryShadowLightId = 0
    $preset.lighting.skyboxEnabled = $false
    $preset.lighting.diffuseIblEnabled = $false
    $preset.lighting.specularIblEnabled = $false
    $preset.lighting.emissiveEnabled = $false
    $preset.shadow.enabled = $true
    $preset.pathTracing = @{
        maxBounces = 1
        directLightingEnabled = $true
        environmentEnabled = $false
        emissiveEnabled = $false
        russianRouletteEnabled = $false
    }
    $presetPath = Join-Path $OutputDirectory "$variant-preset.json"
    [IO.File]::WriteAllText($presetPath, ($preset | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
    $capturePath = Join-Path $OutputDirectory "$variant.pfm"
    $logPath = Join-Path $OutputDirectory "$variant.log"
    Remove-Item -LiteralPath $capturePath, $logPath -Force -ErrorAction SilentlyContinue
    $arguments = @(
        '-SceneFile', "`"$scenePath`"", '-RenderPreset', "`"$presetPath`"",
        '-EnablePathTracing', '-PathTracingSamples', $Samples, '-PathTracingSeed', $Seed,
        '-CapturePath', "`"$capturePath`"", '-LogToFile', "`"$logPath`"", '-ExitAfterCapture'
    )
    $process = Start-Process -FilePath $exe -ArgumentList ($arguments -join ' ') -WorkingDirectory $root -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000))
    {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Timed out: $variant"
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capturePath))
    {
        throw "Capture failed: $variant (exit $($process.ExitCode))"
    }
    $logLines = @(Get-Content -LiteralPath $logPath)
    if (@($logLines | Where-Object { $_ -match '^\[(ERROR|CORRUPTION)\]' }).Count -ne 0)
    {
        throw "D3D12 error: $variant"
    }
    $diagnostic = @($logLines | Where-Object { $_.StartsWith('[PathTracing] ') }) | Select-Object -Last 1
    if (-not $diagnostic)
    {
        throw "Missing Path Tracing diagnostics: $variant"
    }
    $diagnostics = $diagnostic.Substring('[PathTracing] '.Length) | ConvertFrom-Json
    if ($diagnostics.accumulatedSamples -ne $Samples -or $diagnostics.randomSeed -ne $Seed -or $diagnostics.maxBounces -ne 1)
    {
        throw "Unexpected Path Tracing settings: $variant"
    }
    $captures[$variant] = @{ Path = $capturePath; Hash = (Get-FileHash $capturePath -Algorithm SHA256).Hash; Diagnostics = $diagnostics }
}

$clear = Read-Pfm $captures.clear.Path
$between = Read-Pfm $captures.between.Path
$beyond = Read-Pfm $captures.beyond.Path
if ($clear.Width -ne $between.Width -or $clear.Height -ne $between.Height -or
    $clear.Width -ne $beyond.Width -or $clear.Height -ne $beyond.Height)
{
    throw 'PFM dimensions differ.'
}
$centerX = [int]($clear.Width / 2)
$centerY = [int]($clear.Height / 2)
$clearSum = 0.0
$betweenSum = 0.0
$beyondSum = 0.0
$maxBeyondDifference = 0.0
$count = 0
for ($y = $centerY - 24; $y -lt $centerY + 24; $y += 2)
{
    for ($x = $centerX - 24; $x -lt $centerX + 24; $x += 2)
    {
        for ($channel = 0; $channel -lt 3; ++$channel)
        {
            $index = ($y * $clear.Width + $x) * 3 + $channel
            $a = [double]$clear.Values[$index]
            $b = [double]$between.Values[$index]
            $c = [double]$beyond.Values[$index]
            if (-not [double]::IsFinite($a) -or -not [double]::IsFinite($b) -or -not [double]::IsFinite($c))
            {
                throw "Non-finite HDR value at index $index"
            }
            $clearSum += $a
            $betweenSum += $b
            $beyondSum += $c
            $maxBeyondDifference = [Math]::Max($maxBeyondDifference, [Math]::Abs($a - $c))
            ++$count
        }
    }
}
$report = [ordered]@{
    lightType = $LightType
    samples = $Samples
    seed = $Seed
    roi = @(($centerX - 24), ($centerY - 24), 48, 48)
    sampledFloatCount = $count
    clearMean = $clearSum / $count
    betweenMean = $betweenSum / $count
    beyondMean = $beyondSum / $count
    maxBeyondDifference = $maxBeyondDifference
    captures = $captures
}
$reportPath = Join-Path $OutputDirectory 'report.json'
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
Write-Output "Clear=$($report.clearMean), between=$($report.betweenMean), beyond=$($report.beyondMean), max beyond diff=$maxBeyondDifference"
Write-Output "Report: $reportPath"
if ($report.clearMean -le 0.001 -or $report.betweenMean -ge 0.8 * $report.clearMean -or $maxBeyondDifference -gt 1e-6)
{
    throw 'Point-light finite visibility validation failed.'
}
