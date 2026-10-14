param([string]$Lab=(Join-Path $PSScriptRoot 'lab'),[string]$CheckpointSource='')
# Lays the lab's kept save (lab/pristine-save, or another kept save inside the lab) over the lab's own
# mlc01/usr/save, which the automatic runs load (session.py --private-saves). Files are overwritten, none is
# removed. Cemu's own saves are never touched by it.
$ErrorActionPreference='Stop'
$labRoot=[IO.Path]::GetFullPath($Lab)
if($labRoot -ne [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'lab'))){throw 'Only the lab of the bridge is supported'}
if(@(Get-CimInstance Win32_Process -Filter "Name='Cemu.exe'").Count){throw 'Close Cemu before restoring the save of the lab'}
$saved=if($CheckpointSource){[IO.Path]::GetFullPath($CheckpointSource)}else{Join-Path $labRoot 'pristine-save'}
if(-not $saved.StartsWith($labRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'The kept save must be inside the lab'}
if($CheckpointSource -and -not (Test-Path -LiteralPath $saved)){throw 'Declared checkpoint absent'}
if(-not (Test-Path -LiteralPath $saved)) {
    throw 'The lab has no kept save: prepare_lab.ps1 makes it'
}
$target=[IO.Path]::GetFullPath((Join-Path $labRoot 'mlc01/usr/save'))
if(-not $target.StartsWith($labRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Save target escaped the lab'}
if((Get-Item -LiteralPath $target).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Save target must be a folder of the lab itself'}
Copy-Item "$saved/*" -Destination $target -Recurse -Force
Get-ChildItem -LiteralPath $saved -Recurse -File | ForEach-Object {
    $hash=Get-FileHash -LiteralPath $_.FullName
    [pscustomobject]@{Path=$_.FullName.Substring($saved.Length+1);SHA256=$hash.Hash}
} | ConvertTo-Json | Set-Content -LiteralPath "$labRoot/pristine-save-manifest.json"
