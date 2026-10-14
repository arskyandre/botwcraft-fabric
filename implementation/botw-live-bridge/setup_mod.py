"""Makes the mod's Gradle build, once: implementation/botwcraft-fabric/retained (not in Git).

The mod's project is copied there and Gradle is run in the copy, with the Gradle home inside it. Gradle downloads
Minecraft and the mod's libraries (about 1.2 GB, a few minutes), compiles the mod and writes the launch files of
the development client. mod_build.py and client.py work from that folder afterwards (retained.py).

Run it again only when the mod's dependencies change (gradle.properties, build.gradle): move the old `retained`
folder away first; this script does not overwrite one.

    python -B setup_mod.py
"""
import os
import shutil
import subprocess
import sys

import local
import retained


def main():
    home = retained.HOME; source = home.parent
    if home.exists(): raise SystemExit(f'{home} is already there; move it away to make it again')
    java_home = local.java('javac').parent.parent
    ignore = shutil.ignore_patterns('build', '.gradle', 'run')
    shutil.copytree(source / 'fabric', home / 'fabric', ignore=ignore)
    shutil.copytree(source / 'protocol', home / 'protocol')
    environment = dict(os.environ, JAVA_HOME=str(java_home), GRADLE_USER_HOME=str(home / '.gradle-user'))
    command = [str(home / 'fabric/gradlew.bat'), '-p', str(home / 'fabric'), '--no-configuration-cache', '--console=plain', 'build', 'writeClientArgFile']
    print(' '.join(command), flush=True)
    result = subprocess.run(command, env=environment, cwd=home / 'fabric')
    subprocess.run([str(home / 'fabric/gradlew.bat'), '-p', str(home / 'fabric'), '--stop'], env=environment, cwd=home / 'fabric')
    if result.returncode: raise SystemExit('Gradle failed; see its output above')
    for needed in ('.gradle/loom-cache/launch.cfg', '.gradle/loom-cache/log4j.xml', 'build/loom-cache/argFiles/runClient', 'build/classes/java/main'):
        if not (retained.RETAINED / needed).exists(): raise SystemExit(f'Gradle did not make {needed}')
    print('The mod is built. Next: python -B mod_build.py')


if __name__ == '__main__':
    sys.exit(main())
