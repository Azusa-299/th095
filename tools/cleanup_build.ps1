param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = (Resolve-Path -LiteralPath (Join-Path $projectRoot 'build')).Path
$archiveRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'temp/build-history-20261113'))
if ($buildRoot -ne (Join-Path $projectRoot 'build') -or
    -not $archiveRoot.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar)) {
    throw 'Resolved cleanup paths are outside this project.'
}
# Preserve runtime resources, incremental build state, IDA databases, and
# the current acceptance evidence. All historical output is archived.
$keep = @('CMakeFiles','Testing','ida-th95','ida-th95cn','CMakeCache.txt',
    'cmake_install.cmake','CTestTestfile.cmake','DartConfiguration.tcl',
    'Makefile','libth95-core.a','th95.exe','th95-fixtures.exe',
    'th095.dat','th095c.dat','thbgm.dat','score-fixture-full.dat')
$candidates = @(Get-ChildItem -LiteralPath $buildRoot -Force | Where-Object {
    $_.Name -notin $keep -and $_.Name -notlike 'effect-*'
})
$manifest = foreach ($item in $candidates) {
    $sourcePath = [IO.Path]::GetFullPath($item.FullName)
    $targetPath = [IO.Path]::GetFullPath((Join-Path $archiveRoot $item.Name))
    if (-not $sourcePath.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar) -or
        -not $targetPath.StartsWith($archiveRoot + [IO.Path]::DirectorySeparatorChar)) {
        throw "Unexpected source or destination: $sourcePath"
    }
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing reparse point: $sourcePath" }
    $files = if ($item.PSIsContainer) { @(Get-ChildItem -LiteralPath $sourcePath -File -Recurse) } else { @($item) }
    $size = ($files | Measure-Object -Property Length -Sum).Sum
    [pscustomobject]@{Name=$item.Name;Files=$files.Count;Bytes=[long]$size;Source=$sourcePath;Destination=$targetPath}
}
$manifest | Select-Object Name,Files,Bytes | Format-Table
$total = ($manifest | Measure-Object -Property Bytes -Sum).Sum
Write-Output ('Archive {0} entries, {1:N1} MiB; Apply={2}' -f $candidates.Count,($total/1MB),$Apply)
if (-not $Apply) { return }
New-Item -ItemType Directory -Path $archiveRoot -Force | Out-Null
foreach ($entry in $manifest) {
    if (Test-Path -LiteralPath $entry.Destination) { throw "Archive destination already exists: $($entry.Destination)" }
}
$manifest | Export-Csv -LiteralPath (Join-Path $archiveRoot 'manifest.csv') -NoTypeInformation -Encoding utf8
foreach ($entry in $manifest) { Move-Item -LiteralPath $entry.Source -Destination $entry.Destination }
Write-Output 'Historical build outputs archived; no files deleted.'
