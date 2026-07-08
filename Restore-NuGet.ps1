param(
    [string]$NuGetExe = "",
    [string]$PackagesDirectory = ""
)

$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
$packagesConfig = Join-Path $projectRoot "packages.config"

if ($PackagesDirectory -eq "")
{
    $PackagesDirectory = Join-Path $projectRoot "packages"
}

if ($NuGetExe -eq "")
{
    $localNuGet = Join-Path $projectRoot "tools\nuget.exe"
    if (Test-Path -LiteralPath $localNuGet)
    {
        $NuGetExe = $localNuGet
    }
    else
    {
        $nugetCommand = Get-Command nuget.exe -ErrorAction SilentlyContinue
        if ($null -ne $nugetCommand)
        {
            $NuGetExe = $nugetCommand.Source
        }
    }
}

if ($NuGetExe -eq "")
{
    throw "nuget.exe was not found. Put nuget.exe on PATH, place it at tools\nuget.exe, or pass -NuGetExe <path>."
}

if (-not (Test-Path -LiteralPath $packagesConfig))
{
    throw "packages.config was not found: $packagesConfig"
}

& $NuGetExe restore $packagesConfig -PackagesDirectory $PackagesDirectory -NonInteractive
if ($LASTEXITCODE -ne 0)
{
    throw "NuGet restore failed with exit code $LASTEXITCODE."
}
