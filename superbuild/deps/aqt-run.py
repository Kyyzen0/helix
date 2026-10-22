"""Runs aqtinstall with a temporary folder of a known length.

aqtinstall hands work between its processes over a socket, and the path of
that socket is built from the temporary folder. It also asks python for a
usable temporary folder, which python finds by creating and removing a file in
one.

Setting the folder directly skips both. The socket path then stays short
enough for the system to accept, no matter how long the folder python would
have picked on its own.

The folder travels in the environment, because the processes aqtinstall starts
are started from the command line rather than from this script, and they only
inherit the environment.

Usage: AQT_TEMP_DIR=<folder> aqt-run.py <aqt arguments>
"""

import os
import runpy
import sys
import tempfile

_folder = os.environ.get("AQT_TEMP_DIR")
# The folder is only used when it is there. A path that does not exist is not
# usable, and python would rather pick a folder of its own than fail over one
# that is not there.
if _folder and os.path.isdir(_folder):
    tempfile.tempdir = _folder

if __name__ == "__main__":
    runpy.run_module("aqt", run_name="__main__")
