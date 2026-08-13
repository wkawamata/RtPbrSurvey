[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$Port = 8765,
    [switch]$SkipBuild,
    [switch]$SkipCapture,
    [switch]$NoBrowser,
    [switch]$StochasticSampling
)

$ErrorActionPreference = 'Stop'
$validationRoot = $PSScriptRoot
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $validationRoot '..\..\..')).Path
$planFileName = if ($StochasticSampling) { 'capture-plan-stochastic.json' } else { 'capture-plan.json' }
$suiteFileName = if ($StochasticSampling) { 'suite-stochastic.json' } else { 'suite.json' }
$planPath = Join-Path $validationRoot $planFileName
$suiteTemplatePath = Join-Path $validationRoot $suiteFileName
$currentSuitePath = Join-Path $validationRoot 'current-suite.json'
$reportsDirectory = Join-Path $validationRoot 'reports'
$serverStatePath = Join-Path $reportsDirectory 'server-state.json'
$executablePath = Join-Path $repositoryRoot 'bin\x64\Debug\RtPbrSurvey.exe'
$msbuildPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Invoke-CaptureVariant
{
    param(
        [Parameter(Mandatory)]
        [string]$Variant,
        [Parameter(Mandatory)]
        [double]$HistoryWeight
    )

    $logPath = Join-Path $reportsDirectory "capture-$Variant.log"
    $arguments = @(
        '-AutoSelectGltfDamagedHelmet',
        '-LogToFile', $logPath,
        '-ExitAfterCapture',
        '-ReflectionCapturePlan', $planPath,
        '-ReflectionCaptureVariant', $Variant,
        '-ReflectionTemporalWeight', $HistoryWeight.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-ReflectionTemporalNoiseStrength', $(if ($StochasticSampling) { '0.0' } else { '0.5' })
    )
    if ($StochasticSampling)
    {
        $arguments += '-ReflectionStochasticSampling'
    }
    $process = Start-Process -FilePath $executablePath -ArgumentList $arguments -PassThru -WindowStyle Hidden
    Wait-Process -Id $process.Id -Timeout 180
    $process.Refresh()
    if ($process.ExitCode -ne 0)
    {
        throw "Capture variant '$Variant' exited with code $($process.ExitCode)."
    }
    if (Test-Path -LiteralPath $logPath)
    {
        $errors = Select-String -LiteralPath $logPath -Pattern '\[ERROR\]'
        if ($errors)
        {
            throw "Capture variant '$Variant' logged an error: $($errors[0].Line)"
        }
    }
}

function Assert-CaptureOutputs
{
    param([Parameter(Mandatory)][object]$Plan)

    foreach ($capture in $Plan.captures)
    {
        foreach ($variant in @('a', 'b'))
        {
            $relativePath = $capture.path.Replace('{variant}', $variant)
            $outputPath = Join-Path $validationRoot $relativePath
            $item = Get-Item -LiteralPath $outputPath
            if ($item.Length -le 0)
            {
                throw "Capture is empty: $outputPath"
            }
        }
    }
}

function Assert-SuitePlanContract
{
    param(
        [Parameter(Mandatory)][object]$Suite,
        [Parameter(Mandatory)][object]$Plan
    )

    foreach ($capture in $Plan.captures)
    {
        $matchingCases = @($Suite.cases | Where-Object { $_.id -eq $capture.caseId })
        if ($matchingCases.Count -ne 1)
        {
            throw "Capture case '$($capture.caseId)' must match exactly one suite case."
        }
        $testCase = $matchingCases[0]
        $expectedA = $capture.path.Replace('{variant}', 'a').Replace('\\', '/')
        $expectedB = $capture.path.Replace('{variant}', 'b').Replace('\\', '/')
        if ($testCase.images.a.path.Replace('\\', '/') -ne $expectedA -or
            $testCase.images.b.path.Replace('\\', '/') -ne $expectedB)
        {
            throw "Capture paths do not match suite case '$($capture.caseId)'."
        }
    }
}

function Start-ValidationServer
{
    New-Item -ItemType Directory -Path $reportsDirectory -Force | Out-Null
    if (Test-Path -LiteralPath $serverStatePath)
    {
        $existingState = Get-Content -LiteralPath $serverStatePath -Raw -Encoding UTF8 | ConvertFrom-Json
        $existingHealth = $null
        try
        {
            $existingHealth = Invoke-RestMethod `
                -Uri "http://127.0.0.1:$($existingState.port)/api/health" -TimeoutSec 2
        }
        catch
        {
        }
        if ($existingHealth.application -eq 'hybrid-reflection-subjective-validation')
        {
            if ([int]$existingState.port -ne $Port)
            {
                throw "Validation server is already running on port $($existingState.port)."
            }
            return
        }
        Remove-Item -LiteralPath $serverStatePath -Force
    }

    try
    {
        $unexpectedHealth = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/health" -TimeoutSec 1
        if ($unexpectedHealth.application -eq 'hybrid-reflection-subjective-validation')
        {
            throw "Validation server is already running on port $Port without managed state."
        }
        throw "Port $Port is already in use."
    }
    catch [System.Net.WebException]
    {
    }

    $token = [Guid]::NewGuid().ToString('N')
    $serverScript = Join-Path $validationRoot 'server.mjs'
    $process = Start-Process -FilePath 'node' -ArgumentList @($serverScript, '--port', $Port, '--token', $token) `
        -PassThru -WindowStyle Hidden
    $state = [ordered]@{
        port = $Port
        token = $token
        processId = $process.Id
        startedAtUtc = [DateTime]::UtcNow.ToString('o')
    }
    [IO.File]::WriteAllText($serverStatePath, ($state | ConvertTo-Json) + "`r`n", $utf8NoBom)

    $ready = $false
    for ($attempt = 0; $attempt -lt 30; ++$attempt)
    {
        Start-Sleep -Milliseconds 100
        try
        {
            $health = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/health" -TimeoutSec 1
            if ($health.application -eq 'hybrid-reflection-subjective-validation')
            {
                $ready = $true
                break
            }
        }
        catch
        {
        }
    }
    if (!$ready)
    {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $serverStatePath -Force -ErrorAction SilentlyContinue
        throw 'Validation server did not become ready.'
    }
}

Set-Location -LiteralPath $repositoryRoot
New-Item -ItemType Directory -Path $reportsDirectory -Force | Out-Null

if (!$SkipBuild)
{
    & $msbuildPath 'RtPbrSurvey.vcxproj' '/p:Configuration=Debug' '/p:Platform=x64' '/m'
    if ($LASTEXITCODE -ne 0)
    {
        throw "Debug x64 build failed with code $LASTEXITCODE."
    }
}

$plan = Get-Content -LiteralPath $planPath -Raw -Encoding UTF8 | ConvertFrom-Json
$suite = Get-Content -LiteralPath $suiteTemplatePath -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-SuitePlanContract -Suite $suite -Plan $plan
if (!$SkipCapture)
{
    Invoke-CaptureVariant -Variant 'a' -HistoryWeight 0.0
    Invoke-CaptureVariant -Variant 'b' -HistoryWeight 0.9
}
Assert-CaptureOutputs -Plan $plan

$suite.capture.commit = (& git rev-parse HEAD).Trim()
$suite.capture | Add-Member -NotePropertyName 'capturedAtUtc' -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
$suite.capture | Add-Member -NotePropertyName 'capturePlan' -NotePropertyValue $planFileName -Force
$suite.capture | Add-Member -NotePropertyName 'capturePlanSha256' `
    -NotePropertyValue ((Get-FileHash -LiteralPath $planPath -Algorithm SHA256).Hash.ToLowerInvariant()) -Force
$suite.capture | Add-Member -NotePropertyName 'workingTreeDirty' `
    -NotePropertyValue ([bool](& git status --porcelain --untracked-files=no)) -Force
[IO.File]::WriteAllText($currentSuitePath, ($suite | ConvertTo-Json -Depth 20) + "`r`n", $utf8NoBom)

Start-ValidationServer
$url = "http://127.0.0.1:$Port/?suite=current-suite.json"
Write-Host "Hybrid Reflection subjective validation: $url"
Write-Host "Reports directory: $reportsDirectory"
if (!$NoBrowser)
{
    Start-Process $url
}
