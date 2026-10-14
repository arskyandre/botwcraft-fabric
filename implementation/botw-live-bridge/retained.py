"""Where the mod's Gradle build is, and its recorded paths read as this folder's.

The mod is built once with Gradle (docs/building.md): its compiled classes and resources, its launch files and
the Gradle home with Minecraft's libraries are then in implementation/botwcraft-fabric/retained (not in Git).
mod_build.py compiles the changed sources over a copy of it, and client.py starts Minecraft from its launch
files. Gradle writes the folder it ran in into those files; when the retained build was made somewhere else,
`made-in.txt` in it names that folder, and `here` reads a text with that folder replaced by the one it is in.
"""
import pathlib

HOME = pathlib.Path(__file__).resolve().parents[1] / 'botwcraft-fabric/retained'
RETAINED = HOME / 'fabric'
try: MADE_IN = pathlib.PureWindowsPath((HOME / 'made-in.txt').read_text(encoding='utf-8').strip())
except OSError: MADE_IN = pathlib.PureWindowsPath(HOME)
FORMS = (str, lambda p: str(p).replace('\\', '\\\\'), lambda p: p.as_posix())


def here(text):
    for form in FORMS:
        text = text.replace(form(MADE_IN), form(HOME))
    return text


def elsewhere(text):
    """True while a text still names the folder the retained build was made in."""
    if MADE_IN == pathlib.PureWindowsPath(HOME): return False   # made here (setup_mod.py): nothing to translate
    return any(form(MADE_IN) in text for form in FORMS)
