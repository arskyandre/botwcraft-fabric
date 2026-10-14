"""Build this worktree's adapted copy of the Minecraft mod without Gradle.

The retained mod build (implementation/botwcraft-fabric/retained, see retained.py)
is read-only and stays untouched.
Its compiled classes and resources are copied into the worktree's ignored
build directory; every source file that differs from the retained source is
compiled over the copy with the pinned JDK, and the offline collider check is
run. client.py launches from this copy when build/mod-overrides.json exists.
"""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

import retained as retained_build

ROOT = pathlib.Path(__file__).resolve().parent
MOD = ROOT.parent / 'botwcraft-fabric/fabric'
RETAINED = retained_build.RETAINED
import local
JDK = local.java('javac').parent
PARTS = ('classes/java/main', 'classes/java/client', 'resources/main', 'resources/client')


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    import recycle
    build = MOD / 'build'
    for part in PARTS:
        target = build / part
        if target.exists(): recycle.recycle(target)
        shutil.copytree(RETAINED / 'build' / part, target)
    # Resources this worktree changed (a mixin list with a new mixin) go over the copy as they are;
    # the others keep the retained build's processed form.
    resources = []
    for group in ('main', 'client'):
        for source in sorted((MOD / 'src' / group / 'resources').rglob('*')):
            relative = source.relative_to(MOD / 'src' / group / 'resources'); original = RETAINED / 'src' / group / 'resources' / relative
            if source.is_file() and (not original.exists() or original.read_bytes() != source.read_bytes()):
                target = build / 'resources' / group / relative; target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, target); resources.append(str(source.relative_to(MOD)))
    changed = []
    for group in ('main', 'client'):
        for source in sorted((MOD / 'src' / group / 'java').rglob('*.java')):
            original = RETAINED / 'src' / group / 'java' / source.relative_to(MOD / 'src' / group / 'java')
            if not original.exists() or original.read_bytes() != source.read_bytes(): changed.append((group, source))
    main_classes = build / 'classes/java/main'; client_classes = build / 'classes/java/client'
    outputs = {'main': main_classes, 'client': client_classes}
    if changed:
        # Mixins and other classes that use Minecraft compile against the retained run class path
        # (read-only). The development run needs no reference map: the 26.x names are not obfuscated.
        lines = retained_build.here((RETAINED / 'build/loom-cache/argFiles/runClient').read_text()).splitlines()
        libraries = [e for e in lines[lines.index('-classpath') + 1].split(';') if not e.startswith(str(RETAINED / 'build'))]
        libraries.append(next(l.split('=', 1)[1] for l in retained_build.here((RETAINED / '.gradle/loom-cache/launch.cfg').read_text()).splitlines() if l.strip().startswith('fabric.gameJarPath=')))
        # Main first: the client sources compile against it.
        for group, path in (('main', [main_classes]), ('client', [client_classes, main_classes])):
            # Every source of the group, not only the changed ones: javac copies a constant (Proto.VERSION) into the
            # classes that use it, so a class left as it was would go on with the old number (route-342).
            if group == 'main' and not any(g == 'main' for g, s in changed): continue
            sources = [str(s) for s in sorted((MOD / 'src' / group / 'java').rglob('*.java'))]
            arguments = build / f'javac-{group}.args'
            arguments.write_text('-cp\n"' + ';'.join(pathlib.Path(e).as_posix() for e in [str(p) for p in path] + libraries) + '"\n')
            subprocess.run([str(JDK / 'javac.exe'), '--release', '25', '-nowarn', '-proc:none', '-d', str(outputs[group]), '@' + str(arguments)] + sources, check=True)
    check_out = build / 'classes/java/check'
    if check_out.exists(): recycle.recycle(check_out)
    check_out.mkdir(parents=True)
    checks = sorted((MOD / 'src/check/java').rglob('*.java'))
    subprocess.run([str(JDK / 'javac.exe'), '--release', '25', '-nowarn', '-d', str(check_out), '-cp', str(main_classes)] + [str(s) for s in checks], check=True)
    result = subprocess.run([str(JDK / 'java.exe'), '-cp', f'{check_out};{main_classes}', 'dev.botwcraft.world.TriColliderCheck'], capture_output=True, text=True)
    print(result.stdout.strip() or result.stderr.strip())
    if result.returncode: raise SystemExit('collider check failed')
    record = {'retained': str(RETAINED), 'build': str(build), 'check': result.stdout.strip(), 'resources': resources, 'changed': []}
    for group, source in changed:
        relative = source.relative_to(MOD / 'src' / group / 'java').with_suffix('.class')
        retained = RETAINED / 'build/classes/java' / group / relative
        record['changed'].append({'source': str(source.relative_to(MOD)), 'source_sha256': sha(source),
                                  'class_sha256': sha(outputs[group] / relative),
                                  'retained_class_sha256': sha(retained) if retained.exists() else None})
    (ROOT / 'build/mod-overrides.json').write_text(json.dumps(record, indent=2))
    print(json.dumps(record['changed'], indent=2))


if __name__ == '__main__':
    sys.exit(main())
