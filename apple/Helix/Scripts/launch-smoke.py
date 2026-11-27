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
  5. a launch in French (-AppleLanguages "(fr)").

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


def launch(binary, extra, expect=None):
    env = dict(os.environ, HELIX_DEBUG_LAUNCH="1")
    with tempfile.TemporaryFile() as err:
        process = subprocess.Popen(
            [binary, "-ApplePersistenceIgnoreState", "YES", *extra],
            stdout=subprocess.DEVNULL, stderr=err, env=env)
        deadline = time.time() + ALIVE_SECONDS
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
        ("opens a .gltf with its files", [gltf], only(imported_from(gltf))),
        ("French", ["-AppleLanguages", "(fr)"], untitled),
    ]
    failed = False
    for name, extra, expect in runs:
        problem = launch(binary, extra, expect)
        print(f"{'FAIL' if problem else 'ok  '} {name}" + (f": {problem}" if problem else ""))
        failed = failed or problem is not None
    sys.exit(1 if failed else 0)


main()
