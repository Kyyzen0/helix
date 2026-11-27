#!/usr/bin/env python3
"""Launches the built app a few times and fails if it dies at launch.

    python3 -I apple/Helix/Scripts/launch-smoke.py <path to the Helix binary>

The runs, each ending with the process killed (a window flashes on the screen each time):
  1. a plain launch;
  2. a launch that opens a scene (apple/Helix/Samples/Waves.kut);
  3. a launch that opens a 3D file (tests/data/import/textured-quad.glb), which must become a
     new scene holding a geometryImport node that points to the file itself, not to a copy;
  4. the same with textured-quad.gltf, whose .bin and quadrants.png sit beside it and must be
     found (no missing texture);
  5. a launch in French (-AppleLanguages "(fr)");
  6. a launch that imports tests/data/import/textured-quad.glb into the untitled scene after it
     is shown, as the Import button does (HELIX_DEBUG_IMPORT_AFTER_LAUNCH), then makes AppKit
     autosave the edited document and save it to a temporary .kut: AppKit asks the document for
     its content from a background thread, which stopped the app once ("Block was expected to
     execute on queue com.apple.main-thread"). The process must be alive after that, have read
     the scene off the main thread, and the saved file must hold the import node.

  7. a launch that makes a node, edits it, drags a value and asks to frame (HELIX_DEBUG_FRAMING_CHECK):
     only the first node and the explicit request move the camera, an edit and a drag leave it;
  8. a launch that opens a second scene while one is open (HELIX_DEBUG_OPEN_SECOND), answering the
     question "Close it and open this one?" with Close and Open, then with Cancel
     (HELIX_DEBUG_SWITCH): the app must live, the second scene must replace the first in the one
     case and not open in the other.

A 3D file that was opened is a new untitled scene, not a document of the model's file (run 3), and
an import reads the file behind a sheet without holding the main thread (run 6).

A plain launch must open a new untitled scene in a window at once ("helix-launch: untitled
scene" on standard error), not the Open panel; and a launch that opens a file must not leave an
untitled scene behind it.

It must run outside any sandbox that forbids windows. Standard library only. It never uses
`open` and writes nothing to the user's preferences (-ApplePersistenceIgnoreState).
"""
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
ALIVE_SECONDS = 7.0
IMPORT_ALIVE_SECONDS = 20.0


def launch(binary, extra, expect=None, extra_env=None, seconds=ALIVE_SECONDS):
    env = dict(os.environ, HELIX_DEBUG_LAUNCH="1", **(extra_env or {}))
    with tempfile.TemporaryFile() as err:
        process = subprocess.Popen(
            [binary, "-ApplePersistenceIgnoreState", "YES", *extra],
            stdout=subprocess.DEVNULL, stderr=err, env=env)
        deadline = time.time() + seconds
        while time.time() < deadline:
            if process.poll() is not None:
                err.seek(0)
                return f"died at launch with status {process.returncode}\n{err.read().decode(errors='replace')[-2000:]}"
            time.sleep(0.2)
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        err.seek(0)
        log = err.read().decode(errors="replace")
    lines = log.splitlines()
    ok = (expect(lines) if getattr(expect, "whole_log", False) else any(expect(line) for line in lines)) if expect else True
    if not ok:
        return f"alive, but the scene is not what was expected\n{log[-2000:]}"
    return None


def untitled(line):
    return line.startswith("helix-launch: untitled scene")


def big_window(lines):
    """A plain launch opens an untitled scene in a window of about 85 percent of the screen."""
    if not any(untitled(line) for line in lines):
        return False
    for line in lines:
        if line.startswith("helix-launch: window "):
            size, _, screen = line[len("helix-launch: window "):].partition(" of ")
            width, height = (int(v) for v in size.split("x"))
            screen_width, screen_height = (int(v) for v in screen.split("x"))
            return width >= 0.8 * screen_width and height >= 0.8 * screen_height and width <= screen_width
    return False


big_window.whole_log = True


def only(check):
    """The scene the file made, and no untitled scene next to it."""
    def wrapped(lines):
        return any(check(line) for line in lines) and not any(untitled(line) for line in lines)
    wrapped.whole_log = True
    return wrapped


def imported_from(path):
    """The scene holds one geometryImport node whose filePath is `path`, with no missing texture."""
    def check(line):
        return (line.startswith("helix-launch:") and "geometryImport" in line
                and f" paths={path} " in line and "missingTexture=false" in line)
    return check


def imported_then_saved(lines):
    """The import worked, AppKit autosaved and saved off the main thread, and the app lived on."""
    log = "\n".join(lines)
    return ("helix-launch: import-after-launch imported error=none" in log
            and "helix-launch: import-after-launch autosaved error=none" in log
            and "helix-launch: import-after-launch saved error=none" in log
            and log.count("helix-launch: snapshot off the main thread") >= 2
            # The file is read behind the sheet, on another thread: the main thread went on running.
            and "helix-launch: import sheet shown file=textured-quad.glb" in log
            and "helix-launch: import sheet closed file=textured-quad.glb error=none" in log
            and "helix-launch: import main thread longest-gap-ms=" in log)


imported_then_saved.whole_log = True


def framing_follows_the_rule(lines):
    """The first node frames, an edit and a drag leave the camera alone, Frame frames."""
    return any(
        line == "helix-launch: framing first-node=true edit-keeps-camera=true drag-keeps-camera=true explicit=true"
        for line in lines)


framing_follows_the_rule.whole_log = True


def second_scene_replaced_the_first(path):
    """The question was asked about both scenes, answered, and the second scene is the one open."""
    def check(lines):
        log = "\n".join(lines)
        asked = "helix-launch: switch asked current=Waves" in log and "new=textured-quad.glb" in log
        done = "helix-launch: switch done" in log
        last_scene = [line for line in lines if line.startswith("helix-launch: scene nodes=")][-1:]
        return asked and done and bool(last_scene) and "geometryImport" in last_scene[0] and f"paths={path} " in last_scene[0]
    check.whole_log = True
    return check


def second_scene_was_refused(lines):
    """The question was asked and cancelled: the first scene is still the one open."""
    log = "\n".join(lines)
    scenes = [line for line in lines if line.startswith("helix-launch: scene nodes=")]
    return ("helix-launch: switch asked current=Waves" in log and "helix-launch: switch cancelled" in log
            and len(scenes) == 1 and "geometryImport" not in scenes[0])


second_scene_was_refused.whole_log = True


def opened_as_an_untitled_scene(path):
    """A 3D file that was opened is a scene with no file of its own, edited, holding the import."""
    def check(lines):
        log = "\n".join(lines)
        return ("fileURL=none" in log and f"was={os.path.basename(path)} " in log and "edited=true" in log
                and "helix-launch: import sheet shown" in log)
    check.whole_log = True
    return check


def saved_file_holds_the_import(path):
    try:
        with open(path, encoding="utf-8") as file:
            text = file.read()
    except OSError as error:
        return f"the saved scene was not written: {error}"
    return None if "geometryImport" in text else "the saved scene has no import node"


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: python3 -I launch-smoke.py <path to the Helix binary>")
    binary = sys.argv[1].rstrip("/")
    # The bundle works as well as the binary inside it.
    if binary.endswith(".app"):
        binary = os.path.join(binary, "Contents", "MacOS", "Helix")
    waves = os.path.join(ROOT, "apple", "Helix", "Samples", "Waves.kut")
    quad = os.path.join(ROOT, "tests", "data", "import", "textured-quad.glb")
    gltf = os.path.join(ROOT, "tests", "data", "import", "textured-quad.gltf")
    runs = [
        ("plain launch opens an untitled scene in a big window", [], big_window),
        ("opens a .kut, and no untitled scene", [waves],
         only(lambda l: l.startswith("helix-launch: scene nodes=") and "nodes=0 " not in l)),
        ("opens a .glb, and no untitled scene", [quad], only(imported_from(quad))),
        ("a .glb opens as a new untitled scene, with no file of its own", [quad], opened_as_an_untitled_scene(quad)),
        ("opens a .gltf with its files", [gltf], only(imported_from(gltf))),
        ("French", ["-AppleLanguages", "(fr)"], untitled),
    ]
    failed = False
    for name, extra, expect in runs:
        problem = launch(binary, extra, expect)
        print(f"{'FAIL' if problem else 'ok  '} {name}" + (f": {problem}" if problem else ""))
        failed = failed or problem is not None
    extra_runs = [
        ("a node, an edit, a drag and Frame: only the first node and Frame move the camera", [], framing_follows_the_rule,
         {"HELIX_DEBUG_FRAMING_CHECK": "1"}),
        ("a second scene: Close and Open replaces the first", [waves], second_scene_replaced_the_first(quad),
         {"HELIX_DEBUG_OPEN_SECOND": quad, "HELIX_DEBUG_SWITCH": "close"}),
        ("a second scene: Cancel leaves the first", [waves], second_scene_was_refused,
         {"HELIX_DEBUG_OPEN_SECOND": quad, "HELIX_DEBUG_SWITCH": "cancel"}),
    ]
    for name, extra, expect, env in extra_runs:
        problem = launch(binary, extra, expect, env, 10.0)
        print(f"{'FAIL' if problem else 'ok  '} {name}" + (f": {problem}" if problem else ""))
        failed = failed or problem is not None
    with tempfile.TemporaryDirectory() as folder:
        saved = os.path.join(folder, "saved.kut")
        problem = launch(binary, [], imported_then_saved,
                         {"HELIX_DEBUG_IMPORT_AFTER_LAUNCH": quad, "HELIX_DEBUG_SAVE_TO": saved},
                         IMPORT_ALIVE_SECONDS) or saved_file_holds_the_import(saved)
    name = "imports after launch, then autosave and save run off the main thread, and the app lives"
    print(f"{'FAIL' if problem else 'ok  '} {name}" + (f": {problem}" if problem else ""))
    failed = failed or problem is not None
    sys.exit(1 if failed else 0)


main()
