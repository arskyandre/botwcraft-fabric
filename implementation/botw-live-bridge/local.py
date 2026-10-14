"""Where this machine keeps what the bridge needs.

Nothing of one machine's is written into the sources. `local.json` beside this file (not in Git; copy
`local.example.json`) names Cemu, the game and the tools; a tool that is not named there is taken from the PATH.
A relative path in it is read from the project's folder.
"""
import json
import os
import pathlib
import shutil

ROOT = pathlib.Path(__file__).resolve().parent
PROJECT = ROOT.parents[1]
LAB = ROOT / 'lab'
# The Cemu build the bridge was made and tested with (2.6, Windows x64). Its exports are used by name.
CEMU_SHA256 = 'd35da74021799ebffa762b6c29ba0de5d9c1914c9539610b994bfd6499cf3c0d'


def _read():
    try: return json.loads((ROOT / 'local.json').read_text(encoding='utf-8'))
    except FileNotFoundError: return {}


SETTINGS = _read()


def path(name, default=None):
    """The folder or file local.json names under `name`, or None."""
    value = SETTINGS.get(name, default)
    if value in (None, ''): return None
    found = pathlib.Path(os.path.expandvars(str(value)))
    return found if found.is_absolute() else (PROJECT / found).resolve()


def required(name, what):
    found = path(name)
    if found is None or not found.exists():
        raise SystemExit(f'local.json: "{name}" must name {what} (see local.example.json); now: {found}')
    return found


def cemu():
    """Cemu.exe of the user's own installation."""
    return required('cemu', 'Cemu.exe of your Cemu 2.6')


def cemu_data():
    """Where that Cemu keeps settings.xml and graphicPacks (Cemu's own rule: CemuApp.cpp)."""
    named = path('cemu_data')
    if named: return named
    folder = cemu().parent
    if (folder / 'portable').is_dir(): return folder / 'portable'
    if (folder / 'settings.xml').exists(): return folder
    return pathlib.Path(os.environ['APPDATA']) / 'Cemu'


def game():
    """The game's code/U-King.rpx (Breath of the Wild, Wii U, US, with update v208 installed in Cemu)."""
    return required('game', "the game's code/U-King.rpx")


def tool(name, program):
    """A program by its local.json entry, or from the PATH; None when it is on neither."""
    named = path(name)
    if named: return str(named)
    return shutil.which(program)


def needed(name, program):
    found = tool(name, program)
    if not found: raise SystemExit(f'{program} was not found: put it on the PATH or name it in local.json as "{name}"')
    return found


def java(program='java'):
    """java or javac of the JDK (25): local.json "jdk" is the JDK's folder, else JAVA_HOME, else the PATH."""
    home = path('jdk') or (pathlib.Path(os.environ['JAVA_HOME']) if os.environ.get('JAVA_HOME') else None)
    if home and (home / 'bin' / (program + '.exe')).exists(): return home / 'bin' / (program + '.exe')
    found = shutil.which(program)
    if not found: raise SystemExit(f'{program} was not found: name the JDK 25 folder in local.json as "jdk" or set JAVA_HOME')
    return pathlib.Path(found)


def evidence(kind):
    """Where the records of automatic runs go: local.json "evidence", else `runs` in the project (not in Git)."""
    return (path('evidence') or PROJECT / 'runs') / kind


def flag(name, default=False):
    return bool(SETTINGS.get(name, default))


def cemu_mlc():
    """Cemu's own mlc01: the folder its settings name, else mlc01 beside its settings."""
    import xml.etree.ElementTree as E
    data = cemu_data()
    try: named = (E.parse(data / 'settings.xml').findtext('mlc_path') or '').strip()
    except (OSError, E.ParseError): named = ''
    return pathlib.Path(named) if named else data / 'mlc01'
