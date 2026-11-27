#!/usr/bin/env python3
"""Launches the built app a few times and fails if it dies at launch.

    python3 -I apple/Helix/Scripts/launch-smoke.py <path to the Helix binary>

The runs, each ending with the process killed (a window flashes on the screen each time):
  1. a plain launch;
  2. a launch that opens a scene (apple/Helix/Samples/Waves.kut);
  3. a launch that opens a 3D file (tests/data/import/textured-quad.glb), which must become a
     new scene holding a geometryImport node;
  4. a launch in French (-AppleLanguages "(fr)").

It must run outside any sandbox that forbids windows. Standard library only. It never uses
`open` and writes nothing to the user's preferences (-ApplePersistenceIgnoreState).
"""
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
ALIVE_SECONDS = 6.0


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
    if expect and not any(expect(line) for line in log.splitlines()):
        return f"alive, but the scene is not what was expected\n{log[-2000:]}"
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: python3 -I launch-smoke.py <path to the Helix binary>")
    binary = sys.argv[1].rstrip("/")
    # The bundle works as well as the binary inside it.
    if binary.endswith(".app"):
        binary = os.path.join(binary, "Contents", "MacOS", "Helix")
    waves = os.path.join(ROOT, "apple", "Helix", "Samples", "Waves.kut")
    quad = os.path.join(ROOT, "tests", "data", "import", "textured-quad.glb")
    runs = [
        ("plain launch", [], None),
        ("opens a .kut", [waves], lambda l: l.startswith("helix-launch: scene nodes=") and "nodes=0 " not in l),
        ("opens a 3D file", [quad], lambda l: l.startswith("helix-launch:") and "geometryImport" in l),
        ("French", ["-AppleLanguages", "(fr)"], None),
    ]
    failed = False
    for name, extra, expect in runs:
        problem = launch(binary, extra, expect)
        print(f"{'FAIL' if problem else 'ok  '} {name}" + (f": {problem}" if problem else ""))
        failed = failed or problem is not None
    sys.exit(1 if failed else 0)


main()
