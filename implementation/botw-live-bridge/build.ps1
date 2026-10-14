param([switch]$Install,[string]$Headers=(Join-Path $PSScriptRoot 'vendor/Vulkan-Headers'))
# Builds the bridge (build/botw_live_bridge.dll, the shaders and the small fixture). With -Install it is also put
# where a session takes it from: the DLL, its layer file and the fixture into lab, the graphic pack with the
# game's patches into Cemu's own graphicPacks/NativeBridge. Tools come from local.json or the PATH (local.py).
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path lab,build | Out-Null
    # Record this shell and every compiler/validator helper by creation identity.
    @{event='build-shell';pid=$PID;creation_filetime=(Get-Process -Id $PID).StartTime.ToFileTimeUtc();exe=(Get-Process -Id $PID).Path} | ConvertTo-Json -Compress | Add-Content -LiteralPath lab/process-ledger.jsonl
    function Run-Tracked([string[]]$Command) {
        & python -B -c 'import launch,sys,subprocess; sys.exit(launch.run(sys.argv[1:],stderr=subprocess.STDOUT).returncode)' @Command
        if($LASTEXITCODE -ne 0){throw "Build helper failed: $($Command[0])"}
    }
    function From-Local([string]$Expression) {
        $value=& python -B -c "import local;print($Expression)"
        if($LASTEXITCODE -ne 0){throw "local.py: $Expression"}
        $value
    }
    $gxx=From-Local "local.needed('gxx','g++')"
    $glslang=From-Local "local.needed('glslang','glslangValidator')"
    if(-not (Test-Path "$Headers/include/vulkan/vulkan.h")){throw 'Vulkan-Headers are not in vendor/Vulkan-Headers (docs/building.md); or supply -Headers'}
    $revision=& python -B -c 'import launch,sys; r=launch.run(sys.argv[1:],check=True,capture_output=True,text=True); print(r.stdout.strip())' git -C $Headers rev-parse HEAD
    if($revision -ne '9a0f3099c8a9607a7c0f3127d8abfdc19a93e8c5'){throw 'Unexpected Vulkan-Headers revision (9a0f3099 is the pinned one)'}
    Run-Tracked @('python','-B','fixture.py','build/fixture')
    Run-Tracked @('python','-B','finite_patch_check.py')
    foreach($shader in 'mesh.vert','mesh.frag','overlay.vert','overlay.frag','gbuffer.frag','hdr.frag','shadow.frag') {
        Run-Tracked @($glslang,'-V',$shader,'-o',"build/fixture/$shader.spv")
    }
    Run-Tracked @($gxx,'-std=c++23','-O2','-w','-static-libgcc','-static-libstdc++',"-I$Headers/include",'native_check.cpp','-o','build/native_check.exe')
    Run-Tracked @((Join-Path $PSScriptRoot 'build/native_check.exe'))
    Run-Tracked @($gxx,'-std=c++23','-O2','-Wall','-Wextra','-Wno-missing-field-initializers','-shared','-static-libgcc','-static-libstdc++',"-I$Headers/include",'probe.cpp','-o','build/botw_live_bridge.dll')
    if($Install) {
        if(@(Get-CimInstance Win32_Process -Filter "Name='Cemu.exe'").Count){throw 'Close Cemu before installing the bridge'}
        $pack=Join-Path (From-Local 'local.cemu_data()') 'graphicPacks/NativeBridge'
        New-Item -ItemType Directory -Force -Path lab/fixture,$pack | Out-Null
        Copy-Item -LiteralPath build/botw_live_bridge.dll,layer.json -Destination lab -Force
        Copy-Item build/fixture/* -Destination lab/fixture -Force
        Copy-Item pack/* -Destination $pack -Force
        "installed: lab, $pack"
    }
    Get-FileHash build/botw_live_bridge.dll -Algorithm SHA256
} finally {Pop-Location}
