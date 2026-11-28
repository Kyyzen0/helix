#!/usr/bin/env python3
"""End to end check of the live link between an AI agent and the Helix window.

    python3 -I apple/Helix/Scripts/live-smoke.py <path to Helix.app, or the binary inside it>

Launches the app with a new scene (HELIX_DEBUG_LAUNCH=1) on a socket of its own, starts
`Helix --mcp` as the relay of a client that calls itself "codex", and has it build a small graph and
render it. What the app did is read from its standard error, where each live call writes a line:
the nodes of the open document, the display node, the step of undo on top, how many steps the
agents made and where the viewport camera is. Then it takes the steps back one at a time
(HELIX_DEBUG_LIVE_UNDO_FILE), and checks the fallbacks with no app running. It never uses `open` and
writes nothing to the preferences of the user. Standard library only. Exits 0 when every check passes.
"""
import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

failures = []


def check(condition, message):
    print(("ok    " if condition else "FAIL  ") + message)
    if not condition:
        failures.append(message)


class Client:
    """A client of the protocol on the standard input and output of `Helix --mcp`."""

    def __init__(self, binary, env, flags=()):
        self.process = subprocess.Popen(
            [binary, "--mcp", *flags], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, bufsize=1, env=env)
        self.lines = queue.Queue()
        self.raw = []
        self.next_id = 1
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.process.stdout:
            self.raw.append(line)
            self.lines.put(line)
        self.lines.put(None)

    def notify(self, method, params=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

    def request(self, method, params=None, timeout=60):
        identifier = self.next_id
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": identifier, "method": method}
        if params is not None:
            message["params"] = params
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.lines.get(timeout=0.2)
            except queue.Empty:
                continue
            if line is None:
                return None
            reply = json.loads(line)
            if reply.get("id") == identifier:
                return reply
        return None

    def send_call(self, name, arguments=None):
        """Sends a call and does not wait for the reply."""
        identifier = self.next_id
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": identifier, "method": "tools/call", "params": {"name": name, "arguments": arguments or {}}}
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        return identifier

    def call(self, name, arguments=None, timeout=60):
        reply = self.request("tools/call", {"name": name, "arguments": arguments or {}}, timeout)
        return reply["result"] if reply and "result" in reply else None

    def initialize(self, name):
        reply = self.request("initialize", {
            "protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": name, "version": "1"}})
        self.notify("notifications/initialized")
        return reply

    def close(self):
        try:
            self.process.stdin.close()
        except OSError:
            pass
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()


def data(result):
    return result.get("structuredContent") if result else None


class App:
    def __init__(self, binary, env):
        self.err = tempfile.NamedTemporaryFile(delete=False, suffix=".log")
        self.process = subprocess.Popen(
            [binary, "-ApplePersistenceIgnoreState", "YES", "--language", "en"], stdout=subprocess.DEVNULL, stderr=self.err, env=env)

    def log(self):
        with open(self.err.name, errors="replace") as handle:
            return handle.read().splitlines()

    def live_lines(self):
        return [parse(line) for line in self.log() if line.startswith("helix-launch: live tool=")]

    def wait(self, predicate, timeout=20):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if predicate(self.log()):
                return True
            if self.process.poll() is not None:
                return False
            time.sleep(0.1)
        return False

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


def parse(line):
    """`helix-launch: live tool=x error=none nodes=/a,/b ...` as a dict. The error runs to ` nodes=`."""
    body = line[len("helix-launch: live "):]
    fields = {}
    keys = ["tool", "error", "nodes", "display", "undo", "steps", "camera", "frame", "edited"]
    positions = [(body.find(f"{key}="), key) for key in keys]
    positions = sorted(p for p in positions if p[0] >= 0)
    for index, (start, key) in enumerate(positions):
        end = positions[index + 1][0] if index + 1 < len(positions) else len(body)
        fields[key] = body[start + len(key) + 1:end].strip()
    return fields


def touch(path):
    """Asks the app for something it watches a file for (the debug hooks of `AgentLink`)."""
    if os.path.exists(path):
        os.unlink(path)
    open(path, "w").close()


def alive_conversations(app, count_file, timeout=10):
    """How many conversations the app holds alive: it writes the number when the file appears."""
    marker = "helix-launch: live-conversations alive="
    seen = len([l for l in app.log() if l.startswith(marker)])
    touch(count_file)
    app.wait(lambda lines: len([l for l in lines if l.startswith(marker)]) > seen, timeout)
    lines = [l for l in app.log() if l.startswith(marker)]
    return int(lines[-1][len(marker):]) if len(lines) > seen else None


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: python3 -I live-smoke.py <path to Helix.app>")
    binary = sys.argv[1].rstrip("/")
    if binary.endswith(".app"):
        binary = os.path.join(binary, "Contents", "MacOS", "Helix")
    folder = tempfile.mkdtemp(prefix="hx", dir="/tmp")
    socket_path = os.path.join(folder, "mcp.sock")
    undo_file = os.path.join(folder, "undo")
    env = dict(os.environ, HELIX_MCP_SOCKET=socket_path)
    app_env = dict(env, HELIX_DEBUG_LAUNCH="1", HELIX_DEBUG_LIVE_UNDO_FILE=undo_file, HELIX_DEBUG_SWITCH="cancel",
                   HELIX_DEBUG_LIVE_FAIL_AFTER="delete_node",
                   HELIX_DEBUG_LIVE_DISCONNECT_FILE=os.path.join(folder, "disconnect"),
                   HELIX_DEBUG_LIVE_COUNT_FILE=os.path.join(folder, "count"),
                   HELIX_DEBUG_IMPORT_READ_DELAY_MS="3000")
    app = App(binary, app_env)
    try:
        run(binary, env, app, socket_path, undo_file, folder)
    finally:
        app.stop()
        shutil.rmtree(folder, ignore_errors=True)
    # With no app running.
    no_app(binary, env)
    sys.exit(1 if failures else 0)


def run(binary, env, app, socket_path, undo_file, folder):
    ready = app.wait(lambda lines: any(l.startswith("helix-launch: untitled scene") for l in lines)
                     and os.path.exists(socket_path))
    check(ready, "the app opens a new scene and listens on its socket")
    if not ready:
        print("\n".join(app.log()[-20:]))
        return
    check(oct(os.stat(socket_path).st_mode & 0o777) == "0o600", "the socket is private to its owner (0600)")

    agent = Client(binary, env)
    reply = agent.initialize("codex")
    check(reply is not None and reply["result"]["serverInfo"]["name"] == "helix", "the relay reaches the app (initialize)")
    check("Undo" in (reply or {}).get("result", {}).get("instructions", ""), "the live instructions say each call is a step of Undo")
    app.wait(lambda lines: any("client connected: Codex" in l for l in lines), 5)
    check(any("client connected: Codex" in l for l in app.log()), "the app names the client from its initialize (Codex)")
    tools = (agent.request("tools/list") or {}).get("result", {}).get("tools", [])
    check(len(tools) == 26, f"26 tools are listed ({len(tools)})")
    new_scene = next((t for t in tools if t["name"] == "new_scene"), {})
    check("discard_changes" not in json.dumps(new_scene.get("inputSchema", {})), "new_scene takes no discard_changes when live")

    # The scene is new and empty: new_scene has nothing to ask.
    result = data(agent.call("new_scene"))
    check(result is not None and result["switched"] is True, f"new_scene on an untouched scene is already done ({result})")

    # Build a graph.
    grid = data(agent.call("create_node", {"type": "helix::grid"}))
    transform = data(agent.call("create_node", {"type": "helix::transform"}))
    check(grid is not None and transform is not None, "create_node twice")
    names = (grid or {}).get("name"), (transform or {}).get("name")
    linked = agent.call("connect_nodes", {"from": names[0], "to": names[1]})
    check(linked is not None and not linked.get("isError"), "connect_nodes")
    parameters = data(agent.call("list_parameters", {"node": names[1]}))
    floats = [p for p in (parameters or {}).get("parameters", []) if p["type"] == "float" and p["components"] == 3]
    parameter = floats[0]["name"] if floats else "translate"
    done = agent.call("set_parameter", {"node": names[1], "parameter": parameter, "value": [0, 2, 0]})
    check(done is not None and not done.get("isError"), f"set_parameter {parameter}")
    shown = agent.call("set_display_node", {"node": names[1]})
    check(shown is not None and not shown.get("isError"), "set_display_node")

    lines = app.live_lines()
    by_tool = [l["tool"] for l in lines]
    check(by_tool[:6] == ["new_scene"][:0] + ["create_node", "create_node", "connect_nodes", "list_parameters", "set_parameter", "set_display_node"][:6]
          or "create_node" in by_tool, f"the app logged each call ({by_tool})")
    created = [l for l in lines if l["tool"] == "create_node"]
    check(len(created) == 2 and created[-1]["nodes"].count("/") == 2, f"both nodes are in the open document ({created[-1]['nodes'] if created else None})")
    check(created and created[0]["undo"] == "AI: Create Node", f"the step is named for Edit > Undo ({created[0]['undo'] if created else None})")
    check(lines and lines[-1]["display"] == names[1], f"the display node is the transform ({lines[-1]['display'] if lines else None})")
    steps = int(lines[-1]["steps"]) if lines else -1
    check(steps == 5, f"one step of undo per call that changes the scene: 5 calls, {steps} steps")

    # A refused call changes nothing and leaves no step.
    before = app.live_lines()[-1]
    failed = agent.call("create_node", {"type": "nope::nothing"})
    check(failed is not None and failed.get("isError") is True, "an unknown node type is a tool error")
    duplicate = agent.call("create_node", {"type": "helix::grid", "name": names[0]})
    check(duplicate is not None and duplicate.get("isError") is True, "a name that is taken is a tool error")
    after = app.live_lines()[-1]
    check(after["steps"] == before["steps"] and after["nodes"] == before["nodes"],
          f"a refused call leaves no step and no change (steps {before['steps']} -> {after['steps']})")
    check(after["error"] != "none", "the app logged the error")
    # A call that fails after it changed the scene: the app is made to fail delete_node once it has deleted.
    gone = agent.call("delete_node", {"node": names[1]})
    rolled = app.live_lines()[-1]
    check(gone is not None and gone.get("isError") is True, "a call that fails after changing the scene is a tool error")
    check(sorted(rolled["nodes"].split(",")) == sorted(before["nodes"].split(",")) and rolled["steps"] == before["steps"] and rolled["display"] == before["display"],
          f"... and the scene is as it was, with no step ({rolled['nodes']}, display {rolled['display']}, steps {rolled['steps']})")
    check(len(data(agent.call("scene_info"))["links"]) == 1, "... links included")
    check(after["edited"] == "true", "the document is marked as edited")

    # The picture leaves the camera where the person put it.
    time.sleep(2.0)
    agent.call("scene_info")
    camera_before = app.live_lines()[-1]["camera"]
    picture = agent.call("render_view", {"azimuth": 120, "elevation": 10, "width": 320, "height": 240})
    kinds = [c.get("type") for c in (picture or {}).get("content", [])]
    check("image" in kinds, f"render_view returns a picture ({kinds})")
    camera_rendered = app.live_lines()[-1]["camera"]
    time.sleep(0.5)
    agent.call("scene_info")
    camera_after = app.live_lines()[-1]["camera"]
    check(camera_before == camera_rendered == camera_after,
          f"the viewport camera is the same after render_view ({camera_before} / {camera_rendered} / {camera_after})")
    check(app.live_lines()[-1]["display"] == names[1], "the display node is the same after render_view")

    # The playhead moves, with no step of undo.
    steps_before = app.live_lines()[-1]["steps"]
    agent.call("set_frame", {"frame": 5})
    moved = app.live_lines()[-1]
    check(moved["frame"].startswith("5") and moved["steps"] == steps_before, f"set_frame moves the timeline and makes no step ({moved['frame']})")

    # A new scene is the person's to accept: the debug answer cancels, and the scene stays.
    answer = data(agent.call("new_scene", timeout=60))
    check(answer is not None and answer["switched"] is False, f"new_scene on a scene with work asks, and the person kept theirs ({answer})")
    check(len(data(agent.call("scene_info"))["nodes"]) == 2, "the scene of the person is untouched")

    # A headless server of its own never touches the open scene.
    alone = Client(binary, env, ["--headless"])
    alone.initialize("headless-test")
    check(len(data(alone.call("scene_info"))["nodes"]) == 0, "--mcp --headless has a scene of its own, not the open one")
    alone.close()

    # Access withdrawn while a file is being read: the call changes nothing. The read takes three
    # seconds (HELIX_DEBUG_IMPORT_READ_DELAY_MS); the agent is disconnected during them.
    quad = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tests", "data", "import", "textured-quad.glb")
    quad = os.path.normpath(quad)
    check(os.path.exists(quad), "the test file textured-quad.glb exists")
    before_cancel = app.live_lines()[-1]
    importer = Client(binary, env)
    importer.initialize("importer")
    importer.send_call("import_file", {"path": quad})
    shown = app.wait(lambda lines: any("import sheet shown file=textured-quad.glb" in l for l in lines), 10)
    check(shown, "the import of an agent shows its sheet")
    imports_before = len([l for l in app.live_lines() if l["tool"] == "import_file"])
    touch(os.path.join(folder, "disconnect"))
    check(app.wait(lambda lines: any(l.startswith("helix-launch: live-disconnect done") for l in lines), 10), "the agents are disconnected while the file is read")
    finished = app.wait(lambda lines: len([l for l in lines if l.startswith("helix-launch: live tool=import_file")]) > imports_before, 15)
    check(finished, "the call that was reading ends")
    cancelled = app.live_lines()[-1]
    check(cancelled["tool"] == "import_file" and cancelled["error"] != "none", f"... as a refused call ({cancelled['error']})")
    check(cancelled["nodes"] == before_cancel["nodes"], f"the scene has no new node ({cancelled['nodes']})")
    check(cancelled["steps"] == before_cancel["steps"] and cancelled["undo"] == before_cancel["undo"],
          f"... and the undo stack is as it was (steps {before_cancel['steps']} -> {cancelled['steps']}, top {cancelled['undo']})")
    check(any("import sheet cancelled file=textured-quad.glb" in l for l in app.log()), "the import sheet is closed")
    # The app closed the socket, so the relay sees the end of it and exits on its own. A descriptor
    # left open by the app would keep it waiting forever.
    deadline = time.time() + 5
    while importer.process.poll() is None and time.time() < deadline:
        time.sleep(0.1)
    check(importer.process.poll() is not None, "the relay of a disconnected agent exits by itself")
    time.sleep(1.0)
    check(app.live_lines()[-1] == cancelled, "nothing else happened to the scene afterwards")
    importer.close()
    agent.close()

    # Each call is one step of Undo, and the steps come back in order.
    undone = []
    for _ in range(5):
        if os.path.exists(undo_file):
            os.unlink(undo_file)
        open(undo_file, "w").close()
        before_count = len([l for l in app.log() if l.startswith("helix-launch: live-undo")])
        app.wait(lambda lines, n=before_count: len([l for l in lines if l.startswith("helix-launch: live-undo")]) > n, 10)
        undone.append([l for l in app.log() if l.startswith("helix-launch: live-undo")][-1])
    if os.environ.get("LIVE_SMOKE_VERBOSE"):
        print("\n".join(undone))
    names_undone = [u.split("name=")[1].split(" nodes=")[0] for u in undone]
    check(names_undone == ["AI: Set Display Node", "AI: Set Parameter", "AI: Connect Nodes", "AI: Create Node", "AI: Create Node"],
          f"Edit > Undo takes the steps back one call at a time ({names_undone})")
    check(undone[3].split("nodes=")[1].split(" ")[0].count("/") == 1 and undone[4].split("nodes=")[1].split(" ")[0] == "",
          "the last two undos remove the transform, then the grid")

    # A normal import still works afterwards (the scene is empty again: the five steps were undone).
    steps_cancelled = app.live_lines()[-1]["steps"]
    fresh = Client(binary, env)
    fresh.initialize("fresh")
    imported = fresh.call("import_file", {"path": quad}, timeout=60)
    check(imported is not None and not imported.get("isError"), f"import_file works for a client that was not disconnected ({imported and imported.get('isError')})")
    done = app.live_lines()[-1]
    check(done["tool"] == "import_file" and done["error"] == "none" and done["nodes"].count("/") == 1 and done["undo"] == "AI: Import File"
          and int(done["steps"]) == int(steps_cancelled) + 1,
          f"... as one step of undo ({done['nodes']}, {done['undo']}, steps {steps_cancelled} -> {done['steps']})")

    # A conversation that is closed is freed: connect, initialize and leave, several times.
    count_file = os.path.join(folder, "count")
    baseline = alive_conversations(app, count_file)
    for index in range(4):
        visitor = Client(binary, env)
        visitor.initialize(f"visitor-{index}")
        visitor.close()
    freed = None
    for _ in range(20):
        freed = alive_conversations(app, count_file)
        if freed == baseline:
            break
        time.sleep(0.25)
    check(baseline is not None and freed == baseline, f"closed conversations are freed: {baseline} alive before four reconnections, {freed} after")
    fresh.close()

    # No app: the relay says so, or falls back.
    app.stop()


def no_app(binary, env):
    missing = dict(env, HELIX_MCP_SOCKET="/tmp/helix-live-smoke-none.sock")
    live = subprocess.run([binary, "--mcp", "--live"], input="", capture_output=True, text=True, env=missing, timeout=30)
    check(live.returncode != 0 and "no running Helix" in live.stderr and live.stdout == "",
          f"--mcp --live with no app fails clearly ({live.returncode}: {live.stderr.strip()[:80]})")
    both = subprocess.run([binary, "--mcp", "--live", "--headless"], input="", capture_output=True, text=True, env=missing, timeout=30)
    check(both.returncode == 2 and "usage" in both.stderr, "--live and --headless together are refused")
    fallback = Client(binary, missing)
    reply = fallback.initialize("fallback-test")
    instructions = (reply or {}).get("result", {}).get("instructions", "")
    check(reply is not None and "Undo" not in instructions, "--mcp with no app serves headless")
    info = data(fallback.call("scene_info"))
    check(info is not None and info["nodes"] == [], "the headless server has an empty scene of its own")
    fallback.close()
    check(all(json.loads(line) for line in fallback.raw), "standard output holds the protocol and nothing else")


main()
