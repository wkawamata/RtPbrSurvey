param(
    [Parameter(Mandatory = $true)][string]$Dxc,
    [Parameter(Mandatory = $true)][string]$Shader
)
$ErrorActionPreference = 'Stop'
$dump = (& $Dxc -dumpbin $Shader) -join "`n"
if ($LASTEXITCODE -ne 0)
{
    throw 'DXC reflection failed.'
}
$bindings = @{
    'gIn_Tiles' = 't0'; 'gIn_Normal_Roughness' = 't1'; 'gIn_ViewZ' = 't2';
    'gIn_Spec' = 't3'; 'gIn_Diff' = 't4'; 'gOut_Spec' = 'u0'; 'gOut_Diff' = 'u1'
}
foreach ($binding in $bindings.GetEnumerator())
{
    $pattern = '(?m)^;\s+' + [regex]::Escape($binding.Key) + '\s+.*\s' + $binding.Value + '\s+1\s*$'
    if ([regex]::Matches($dump, $pattern).Count -ne 1)
    {
        throw "Unexpected reconstruction binding: $($binding.Key)"
    }
}
if ($dump -notmatch 'NumThreads=\(8,8,1\)' -or
    $dump -notmatch '(?m)^;\s+RELAX_HitDistReconstructionConstants\s+cbuffer\s+.*cb0,space1\s+1\s*$' -or
    $dump -notmatch 'RELAX_HitDistReconstructionConstants;\s+; Offset:\s+0 Size:\s+712')
{
    throw 'Unexpected pinned reconstruction thread group or constant-buffer ABI.'
}
$hash = (Get-FileHash -LiteralPath $Shader -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "Contract passed: $Shader SHA256=$hash"
Write-Output 'Compile/reflection only; no GPU dispatch or reconstruction quality approval.'
