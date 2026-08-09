[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$statePath = Join-Path $PSScriptRoot 'reports\server-state.json'
if (!(Test-Path -LiteralPath $statePath))
{
    Write-Host 'Hybrid Reflection validation server is not running.'
    return
}

$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$headers = @{ 'X-Shutdown-Token' = $state.token }
try
{
    Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$($state.port)/api/shutdown" -Headers $headers | Out-Null
}
finally
{
    Remove-Item -LiteralPath $statePath -Force
}
Write-Host 'Hybrid Reflection validation server stopped.'
