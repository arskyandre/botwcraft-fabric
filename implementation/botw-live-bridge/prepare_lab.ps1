param([Parameter(Mandatory)][string]$CheckpointSource)
# For automatic runs only (docs/testing.md); playing needs none of this.
#
# An automatic run loads a known save again and again. So that it never writes a save of yours, it runs Cemu with
# the lab's own mlc01 (Cemu's --mlc). This script makes that folder once:
#   lab/mlc01/usr/title, lab/mlc01/sys/title   links to the same folders of Cemu's own mlc01: the installed game,
#                                               its update and DLC are used where they are, nothing is copied;
#   lab/mlc01/usr/save, lab/pristine-save       copies of -CheckpointSource, a folder of the form of Cemu's
#                                               mlc01/usr/save holding the save the runs are to load.
$ErrorActionPreference='Stop'
$labRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'lab'))
if(Test-Path -LiteralPath "$labRoot/mlc01"){throw 'lab/mlc01 is already there; this script does not overwrite it'}
if(-not (Test-Path -LiteralPath $CheckpointSource -PathType Container)){throw '-CheckpointSource must be a folder of saves'}
Push-Location $PSScriptRoot
try {$mlc=& python -B -c 'import local;print(local.cemu_mlc())';if($LASTEXITCODE -ne 0){throw 'local.py could not find Cemu (local.json)'}} finally {Pop-Location}
New-Item -ItemType Directory -Force -Path "$labRoot/mlc01/usr","$labRoot/mlc01/sys" | Out-Null
# A link is made to where the folder really is, should the one of Cemu be a link itself.
function End-Of([string]$path){$item=Get-Item -LiteralPath $path -Force;while($item.LinkType){$item=Get-Item -LiteralPath ([string]$item.Target[0]) -Force};$item.FullName}
New-Item -ItemType Junction -Path "$labRoot/mlc01/usr/title" -Target (End-Of "$mlc/usr/title") | Out-Null
New-Item -ItemType Junction -Path "$labRoot/mlc01/sys/title" -Target (End-Of "$mlc/sys/title") | Out-Null
Copy-Item -LiteralPath $CheckpointSource -Destination "$labRoot/mlc01/usr/save" -Recurse
Copy-Item -LiteralPath $CheckpointSource -Destination "$labRoot/pristine-save" -Recurse
"lab/mlc01 is ready; its saves are copies of $CheckpointSource"
