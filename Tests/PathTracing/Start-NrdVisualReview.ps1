[CmdletBinding()]
param(
    [ValidateSet('Disocclusion', 'StableRadiance', 'Glossy')]
    [string]$Scene = 'Disocclusion',
    [ValidateSet('Nrd', 'Native')]
    [string]$InitialMode = 'Nrd',
    [string]$ExecutablePath,
    [string]$OutputDirectory,
    [switch]$DryRun,
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'
if ($DryRun -and $Launch)
{
    throw '-DryRun and -Launch are mutually exclusive.'
}
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($ExecutablePath))
{
    $ExecutablePath = Join-Path $root 'build\Debug\RtPbrSurvey.exe'
}
$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf))
{
    throw "Executable not found: $ExecutablePath"
}
$sceneDirectoryName = switch ($Scene)
{
    'Disocclusion' { '14-nrd-disocclusion' }
    'StableRadiance' { '13-nrd-stable-radiance' }
    'Glossy' { '16-nrd-glossy-reflections' }
}
$sceneDirectory = Join-Path $root "Assets\Scene\PathTracingValidation\$sceneDirectoryName"
$scenePath = Join-Path $sceneDirectory 'scene.json'
$preset = Get-Content -LiteralPath (Join-Path $sceneDirectory 'render-preset.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$preset.pathTracing.accumulate = $true
$preset.pathTracing.samplesPerFrame = 1
$preset.pathTracing.pixelCenterPrimary = $true
$preset.pathTracing.denoiserDistanceDiagnostics = $true
$preset.pathTracing.debugOutput = 3
$preset.pathTracing.nrdPreview = $InitialMode -eq 'Nrd'
if ([string]::IsNullOrWhiteSpace($OutputDirectory))
{
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $OutputDirectory = Join-Path $root "bin\PathTracingValidation\nrd-visual-$stamp"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $OutputDirectory) -and
    ((Get-Item -LiteralPath $OutputDirectory) -isnot [IO.DirectoryInfo] -or
        @(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count -ne 0))
{
    throw 'OutputDirectory must be a new or empty directory.'
}
$presetPath = Join-Path $OutputDirectory 'review-preset.json'
$logPath = Join-Path $OutputDirectory 'runtime.log'
$arguments = @('-SceneFile', $scenePath, '-RenderPreset', $presetPath, '-EnablePathTracing',
    '-NrdContinuousCameraHistory', '-EnableDebugTexturePreview', '-LogToFile', $logPath)
$manifest = [ordered]@{
    schemaVersion = 1
    status = if ($DryRun) { 'dry-run' } else { 'prepared' }
    scene = $Scene
    sceneId = $sceneDirectoryName
    initialMode = $InitialMode
    executable = $ExecutablePath
    executableSha256 = (Get-FileHash -LiteralPath $ExecutablePath -Algorithm SHA256).Hash.ToLowerInvariant()
    scenePath = $scenePath
    sceneSha256 = (Get-FileHash -LiteralPath $scenePath -Algorithm SHA256).Hash.ToLowerInvariant()
    workingDirectory = $root
    arguments = $arguments
    preset = $preset
    outputDirectory = $OutputDirectory
    subjectiveVerdict = 'unanswered'
}
if ($DryRun)
{
    $manifest | ConvertTo-Json -Depth 30
    return
}

function Write-ReviewJson([string]$Path, $Value)
{
    $text = ($Value | ConvertTo-Json -Depth 30).Replace("`r`n", "`n").Replace("`n", "`r`n") + "`r`n"
    [IO.File]::WriteAllText($Path, $text, [Text.UTF8Encoding]::new($false))
}

[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
Write-ReviewJson $presetPath $preset
$reviewDirectory = Join-Path $root 'Tests\SubjectiveValidation\NrdRelax'
$results = Get-Content -LiteralPath (Join-Path $reviewDirectory 'results-template.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$results.cases = @($results.cases | Where-Object { $_.scene -eq $Scene })
$results | Add-Member -NotePropertyName sessionManifest -NotePropertyValue 'session.json'
Write-ReviewJson (Join-Path $OutputDirectory 'results.json') $results
Copy-Item -LiteralPath (Join-Path $reviewDirectory 'README.md') -Destination (Join-Path $OutputDirectory 'checklist.md')
$manifest['presetSha256'] = (Get-FileHash -LiteralPath $presetPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-ReviewJson (Join-Path $OutputDirectory 'session.json') $manifest
if ($Launch)
{
    $existing = @(Get-Process -Name RtPbrSurvey -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -and [StringComparer]::OrdinalIgnoreCase.Equals($_.Path, $ExecutablePath)
    })
    if ($existing.Count -ne 0)
    {
        throw 'This executable is already running; no duplicate review process was started.'
    }
    $quoted = @($arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' })
    $process = Start-Process -FilePath $ExecutablePath -ArgumentList $quoted -WorkingDirectory $root -WindowStyle Normal -PassThru
    $manifest.status = 'launched-not-visually-verified'
    $manifest['processId'] = $process.Id
    Write-ReviewJson (Join-Path $OutputDirectory 'session.json') $manifest
}
$manifest | ConvertTo-Json -Depth 30
