param(
    [int[]]$Instances = @(1, 10, 30, 60, 121),
    [ValidateRange(1, 1000000)][int]$Frames = 120,
    [ValidateRange(0, 1000000)][int]$WarmupFrames = 120,
    [ValidateRange(1, 20)][int]$Repeats = 5,
    [ValidateSet('raster', 'hybrid')][string]$Mode = 'raster',
    [ValidateSet('on', 'off')][string]$Instancing = 'off',
    [ValidateSet('full', 'constant')][string]$GBuffer = 'full',
    [ValidateSet('on', 'off')][string]$DepthPrepass = 'on',
    [ValidateSet('forward', 'reverse')][string]$DrawOrder = 'forward',
    [ValidateRange(64, 4096)][int]$Width = 1920,
    [ValidateRange(64, 4096)][int]$Height = 1080,
    [string]$OutputDirectory = "",
    [string]$Executable = "",
    [switch]$VSync
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Executable) { $Executable = Join-Path $root 'bin/x64/Release/RtPbrSurvey.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root ('bin/Benchmark/' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
if (Get-Process RtPbrSurvey -ErrorAction SilentlyContinue) { throw 'Close all RtPbrSurvey processes before benchmarking.' }
foreach ($count in $Instances) { if ($count -lt 0 -or $count -gt 1000) { throw 'Instances must be in [0, 1000].' } }
$metadata = @{ revision = (git -C $root rev-parse HEAD); workingTree = @(git -C $root status --short);
    started = (Get-Date -Format o); executable = $Executable; executableSha256 = (Get-FileHash -LiteralPath $Executable).Hash }
try { $metadata.videoControllers = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion) }
catch { $metadata.videoControllerQueryError = $_.Exception.Message }
$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'environment.json') -Encoding utf8
$summary = @()
foreach ($count in $Instances)
{
    for ($repeat = 1; $repeat -le $Repeats; ++$repeat)
    {
        $reportPath = Join-Path $OutputDirectory "helmet-$count-run-$repeat.json"
        if (Test-Path -LiteralPath $reportPath) { throw "Report exists: $reportPath" }
        $arguments = @('-BenchmarkReport', ('"' + $reportPath + '"'), '-BenchmarkInstances', $count,
            '-BenchmarkMode', $Mode,
            '-BenchmarkFrames', $Frames, '-BenchmarkWarmupFrames', $WarmupFrames,
            '-WindowSize', $Width, $Height, '-DisableD3D12DebugLayer')
        if (!$VSync) { $arguments += '-DisableVSync' }
        $arguments += @('-BenchmarkInstancing', $Instancing)
        $arguments += @('-BenchmarkGBuffer', $GBuffer)
        $arguments += @('-BenchmarkDepthPrepass', $DepthPrepass)
        $arguments += @('-BenchmarkDrawOrder', $DrawOrder)
        $process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru
        if (!$process.WaitForExit(300000)) { $process.Kill(); throw "Benchmark timeout: $count instances" }
        if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $reportPath)) { throw "Benchmark failed: $count instances, exit=$($process.ExitCode)" }
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        if ($report.frames -ne $Frames -or $report.instances -ne $count -or $report.metrics.'GPU Total'.samples -ne $Frames) { throw 'Incomplete benchmark report.' }
        if ($report.depthPrepass -ne ($DepthPrepass -eq 'on')) { throw 'Depth prepass mismatch.' }
        if ($report.drawOrder -ne $DrawOrder -or $report.gbufferPipelineStatistics.samples -ne $Frames) { throw 'Pipeline statistics/order mismatch.' }
        if ($report.mode -ne $Mode) { throw 'Benchmark mode mismatch.' }
        if ($report.gbufferShader -ne $GBuffer) { throw 'GBuffer shader mismatch.' }
        if ($report.renderWidth -ne $Width -or $report.renderHeight -ne $Height -or
            $report.outputWidth -ne $Width -or $report.outputHeight -ne $Height) { throw 'Benchmark resolution mismatch.' }
        if ($report.geometryHeap -ne 'default' -or $report.instancing -ne ($Instancing -eq 'on')) { throw 'Geometry settings mismatch.' }
        if ($Mode -eq 'raster')
        {
            $passes = @($report.metrics.PSObject.Properties.Name | Where-Object { $_ -match '^GPU\[' } | Sort-Object)
            $expected = @('GPU[1] Clear', 'GPU[2] Depth PrePass', 'GPU[3] GBufferPass', 'GPU[4] LightPass', 'GPU[5] ToneMapPass')
            if ($DepthPrepass -eq 'off') { $expected = @('GPU[1] Clear', 'GPU[2] GBufferPass', 'GPU[3] LightPass', 'GPU[4] ToneMapPass') }
            if (@(Compare-Object $passes $expected).Count -ne 0) { throw "Unexpected raster passes: $passes" }
        }
        foreach ($metric in $report.metrics.PSObject.Properties)
        {
            if ($metric.Value.samples -ne $Frames -or $metric.Value.averageMs -lt ($metric.Value.min - 0.000001) -or
                $metric.Value.averageMs -gt ($metric.Value.max + 0.000001)) { throw "Invalid metric: $($metric.Name)" }
        }
        $row = [ordered]@{ Instances = $count; Repeat = $repeat }
        foreach ($metric in $report.metrics.PSObject.Properties) { $row[$metric.Name + ' (ms)'] = $metric.Value.averageMs }
        $row['GBuffer PS invocations (count)'] = $report.gbufferPipelineStatistics.averagePsInvocations
        $summary += [pscustomobject]$row
        $summary | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'summary.csv') -NoTypeInformation -Encoding utf8
        Write-Output "Completed: instances=$count repeat=$repeat CPU=$($report.metrics.'CPU Frame'.averageMs) GPU=$($report.metrics.'GPU Total'.averageMs)"
    }
}
Write-Output "Reports: $OutputDirectory"

$averages = foreach ($group in ($summary | Group-Object Instances))
{
    foreach ($name in $group.Group[0].PSObject.Properties.Name)
    {
        if ($name -in @('Instances', 'Repeat')) { continue }
        $values = @($group.Group | ForEach-Object { [double]$_.PSObject.Properties[$name].Value })
        $stats = $values | Measure-Object -Average -Minimum -Maximum
        [pscustomobject]@{ Instances = [int]$group.Name; Metric = $name; Runs = $values.Count;
            Average = $stats.Average; MinRunAverage = $stats.Minimum; MaxRunAverage = $stats.Maximum }
    }
}
$averages | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'averages.csv') -NoTypeInformation -Encoding utf8
