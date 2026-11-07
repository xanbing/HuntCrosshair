param([string]$DeliveryRoot = (Join-Path $PSScriptRoot 'dist'))
$ErrorActionPreference = 'Stop'
$exePath = Join-Path $PSScriptRoot 'build\HuntCrosshair-v1.9.exe'
$readmePath = Join-Path $PSScriptRoot 'README.md'
if (-not (Test-Path -LiteralPath $exePath)) { throw 'Run build.ps1 before packaging.' }
New-Item -ItemType Directory -Force -Path $DeliveryRoot | Out-Null
$DeliveryRoot = (Resolve-Path -LiteralPath $DeliveryRoot).Path
$portableStage = Join-Path $PSScriptRoot 'package\portable-v1.9'
New-Item -ItemType Directory -Force -Path $portableStage | Out-Null
Copy-Item -LiteralPath $exePath -Destination (Join-Path $portableStage 'HuntCrosshair.exe') -Force
Copy-Item -LiteralPath $readmePath -Destination (Join-Path $portableStage '使用说明.md') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LICENSE'),(Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.md') -Destination $portableStage -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination $portableStage -Recurse -Force
Copy-Item -LiteralPath $exePath -Destination (Join-Path $DeliveryRoot 'HuntCrosshair-v1.9.exe') -Force
$portableZip = Join-Path $DeliveryRoot 'HuntCrosshair-Portable-v1.9.zip'
Compress-Archive -LiteralPath (Join-Path $portableStage 'HuntCrosshair.exe'),(Join-Path $portableStage '使用说明.md'),(Join-Path $portableStage 'LICENSE'),(Join-Path $portableStage 'THIRD_PARTY_NOTICES.md'),(Join-Path $portableStage 'licenses') -DestinationPath $portableZip -Force
$sourceZip = Join-Path $DeliveryRoot 'HuntCrosshair-Source-v1.9.zip'
$sourceFiles = @('.gitignore','LICENSE','THIRD_PARTY_NOTICES.md','build.ps1','package.ps1','README.md') | ForEach-Object { Get-Item -LiteralPath (Join-Path $PSScriptRoot $_) }
$sourceFiles += Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -File | Where-Object { $_.Extension -in '.cpp','.h','.ico','.rc','.manifest' }
$sourceFiles += Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'tests') -File | Where-Object { $_.Extension -in '.cpp','.md' -and $_.Name -notin 'ui_logic_tests.cpp','ui_snapshot.cpp','ui_snapshot.md' }
$sourceFiles += Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'tools') -File | Where-Object { $_.Extension -eq '.py' }
$workflowPath = Join-Path $PSScriptRoot '.github'
if (Test-Path -LiteralPath $workflowPath) {
    $sourceFiles += Get-ChildItem -LiteralPath $workflowPath -File -Recurse | Where-Object { $_.Extension -in '.yml','.yaml' }
}
$sourceFiles += Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'docs') -File -Recurse | Where-Object { $_.Extension -in '.md','.png' }
$sourceFiles += Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'licenses') -File -Recurse | Where-Object { $_.Extension -eq '.txt' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $sourceZip) { Remove-Item -LiteralPath $sourceZip -Force }
$archive = [System.IO.Compression.ZipFile]::Open($sourceZip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($source in $sourceFiles) {
        $entryName = $source.FullName.Substring($PSScriptRoot.Length + 1).Replace('\','/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$source.FullName,$entryName,[System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
$hashes = @('HuntCrosshair-v1.9.exe','HuntCrosshair-Portable-v1.9.zip','HuntCrosshair-Source-v1.9.zip') | ForEach-Object {
    $item = Get-FileHash -LiteralPath (Join-Path $DeliveryRoot $_) -Algorithm SHA256
    "$($item.Hash.ToLowerInvariant())  $_"
}
$hashes | Set-Content -LiteralPath (Join-Path $DeliveryRoot 'SHA256-v1.9.txt') -Encoding utf8
Get-Item -LiteralPath (Join-Path $DeliveryRoot 'HuntCrosshair-v1.9.exe'),$portableZip,$sourceZip | Select-Object Name,Length,FullName
