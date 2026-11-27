#!/usr/bin/env python3
"""End to end check of `Helix --mcp`.

    python3 -I apple/Helix/Scripts/mcp-smoke.py <path to the Helix binary>

Starts the server, talks to it in JSON-RPC over a pipe, and checks the protocol and the tools
against a real engine. Standard library only. Exits 0 when every check passes.
"""

import base64
import json
import os
import queue
import struct
import subprocess
import sys
import tempfile
import threading

EXPECTED_TOOLS = {
    "scene_info", "new_scene", "open_scene", "save_scene", "list_node_types", "create_node",
    "delete_node", "connect_nodes", "disconnect_nodes", "set_display_node", "list_parameters",
    "set_parameter", "import_file", "set_frame", "geometry_summary", "read_attribute", "render_view",
}

failures = []


def check(condition, message):
    if condition:
        print("ok    " + message)
    else:
        print("FAIL  " + message)
        failures.append(message)


class Server:
    def __init__(self, binary):
        self.process = subprocess.Popen(
            [binary, "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1,
        )
        self.lines = queue.Queue()
        self.raw = []
        self.next_id = 1
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.process.stdout:
            self.raw.append(line)
            self.lines.put(line)
        self.lines.put(None)

    def send_raw(self, text):
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def notify(self, method, params=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        self.send_raw(json.dumps(message))

    def receive(self, timeout=120):
        line = self.lines.get(timeout=timeout)
        return None if line is None else json.loads(line)

    def request(self, method, params=None):
        ident = self.next_id
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": ident, "method": method}
        if params is not None:
            message["params"] = params
        self.send_raw(json.dumps(message))
        reply = self.receive()
        assert reply is not None and reply.get("id") == ident, "unexpected reply: %r" % (reply,)
        return reply

    def call(self, name, arguments=None):
        reply = self.request("tools/call", {"name": name, "arguments": arguments or {}})
        return reply["result"]

    def data(self, name, arguments=None):
        result = self.call(name, arguments)
        assert not result.get("isError"), "%s failed: %s" % (name, result["content"][0].get("text"))
        return result["structuredContent"]


def png_size(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    return struct.unpack(">II", data[16:24])


def render(server, **arguments):
    result = server.call("render_view", arguments)
    assert not result.get("isError"), result["content"][0].get("text")
    image = next(item for item in result["content"] if item["type"] == "image")
    return image, base64.b64decode(image["data"])


def main(binary):
    server = Server(binary)

    # The lifecycle.
    reply = server.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                          "clientInfo": {"name": "smoke", "version": "0"}})
    result = reply["result"]
    check(result["protocolVersion"] == "2025-11-25", "initialize echoes a protocol version it speaks")
    check(result["serverInfo"]["name"] == "helix" and result["serverInfo"]["version"], "serverInfo names helix")
    check("tools" in result["capabilities"] and result.get("instructions"), "capabilities and instructions")
    check(server.request("initialize", {"protocolVersion": "1999-01-01"})["result"]["protocolVersion"] == "2025-06-18",
          "an unknown protocol version falls back to 2025-06-18")
    server.notify("notifications/initialized")
    check(server.request("ping")["result"] == {}, "ping answers an empty result")

    tools = server.request("tools/list")["result"]["tools"]
    names = {tool["name"] for tool in tools}
    check(EXPECTED_TOOLS <= names, "tools/list has every tool (missing: %s)" % sorted(EXPECTED_TOOLS - names))
    check(all(tool["inputSchema"].get("type") == "object" and isinstance(tool["inputSchema"].get("properties"), dict)
              and tool["description"] and isinstance(tool.get("annotations"), dict) for tool in tools),
          "every tool has a description, an object schema and annotations")

    # Building a scene.
    check(server.data("scene_info")["nodes"] == [], "the scene starts empty")
    check("helix::cube" in server.data("list_node_types")["types"], "list_node_types has helix::cube")
    cube = server.data("create_node", {"type": "helix::cube", "name": "box"})
    check(cube["name"] == "box" and cube["path"] == "/box" and cube["type"] == "helix::cube" and cube["id"],
          "create_node returns id, name, path and type")
    check(server.data("scene_info")["modified"] is True, "the scene is modified after a change")

    parameters = {p["name"]: p for p in server.data("list_parameters", {"node": "box"})["parameters"]}
    check(parameters["size"]["components"] == 3 and parameters["size"]["type"] == "float", "list_parameters shows size")
    value = server.data("set_parameter", {"node": "/box", "parameter": "size", "value": [2, 4, 6]})["value"]
    check(value == [2.0, 4.0, 6.0], "set_parameter reads the vector back: %r" % (value,))
    value = server.data("set_parameter", {"node": cube["id"], "parameter": "size", "value": 8, "component": 0})["value"]
    check(value[0] == 8.0 and value[1] == 4.0, "set_parameter sets one component, by id")

    check(server.call("geometry_summary")["isError"] is True, "geometry_summary with no display node is an error")
    server.data("set_display_node", {"node": "box"})
    summary = server.data("geometry_summary")
    check(summary["point_count"] == 8 and summary["face_count"] == 6 and summary["triangle_count"] == 12,
          "geometry_summary counts the cube")
    bounds = summary["bounds"]
    check(bounds["min"] == [-4.0, -2.0, -3.0] and bounds["max"] == [4.0, 2.0, 3.0] and bounds["size"] == [8.0, 4.0, 6.0],
          "the bounds follow the size parameter: %r" % (bounds,))
    position = [a for a in summary["attributes"] if a["owner"] == "point" and a["name"] == "Position"]
    check(len(position) == 1 and position[0]["type"] == "vector3" and position[0]["count"] == 8,
          "the attribute list has the point Position")

    page = server.data("read_attribute", {"owner": "point", "name": "Position", "count": 3})
    check(page["total_count"] == 8 and page["count"] == 3 and len(page["values"]) == 3 and page["next_start"] == 3
          and all(len(v) == 3 for v in page["values"]), "read_attribute returns the first page of positions")
    rest = server.data("read_attribute", {"owner": "point", "name": "Position", "start": 3, "count": 10000})
    check(rest["count"] == 5 and rest["next_start"] is None, "the last page has no next_start")
    check(server.call("read_attribute", {"owner": "point", "name": "nope"})["isError"] is True,
          "reading an unknown attribute is an error")
    check(server.call("read_attribute", {"owner": "wheel", "name": "P"})["isError"] is True, "a bad owner is an error")

    # Pictures.
    image, first = render(server, width=320, height=200)
    check(image["mimeType"] == "image/png" and png_size(first) == (320, 200), "render_view returns a 320 x 200 PNG")
    _, second = render(server, width=320, height=200, azimuth=40, elevation=30)
    check(png_size(second) == (320, 200) and first != second, "another azimuth gives a different picture")
    _, again = render(server, width=320, height=200, azimuth=40, elevation=30)
    check(second == again, "the same view twice gives the same bytes")
    _, capped = render(server, width=5000, height=64)
    check(png_size(capped) == (2048, 64), "the size is capped at 2048")
    check(server.data("scene_info")["display_node"]["name"] == "box", "render_view leaves the display node alone")

    # A second node, rendered without becoming the display node.
    spare = server.data("create_node", {"type": "helix::cube", "name": "other"})
    _, other = render(server, node="other", width=320, height=200)
    check(png_size(other) == (320, 200), "render_view draws a node that is not displayed")
    check(server.data("scene_info")["display_node"]["name"] == "box", "and puts the display node back")
    server.data("delete_node", {"node": spare["id"]})

    # Links.
    server.data("create_node", {"type": "helix::transform", "name": "mover"})
    server.data("connect_nodes", {"from": "box", "to": "mover"})
    check(len(server.data("scene_info")["links"]) == 1, "connect_nodes makes a link")
    server.data("disconnect_nodes", {"from": "box", "to": "mover"})
    check(server.data("scene_info")["links"] == [], "disconnect_nodes removes it")
    server.data("delete_node", {"node": "mover"})

    # Save, new, open.
    with tempfile.TemporaryDirectory() as folder:
        obj = os.path.join(folder, "triangle.obj")
        with open(obj, "w") as handle:
            handle.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
        imported = server.data("import_file", {"path": obj})
        check(imported["node"]["id"] and imported["summary"] and ".obj" in imported["supported_extensions"],
              "import_file creates a node and gives the summary")
        check(server.data("geometry_summary", {"node": imported["node"]["name"]})["point_count"] == 3,
              "the imported node cooks into its three points")
        server.data("delete_node", {"node": imported["node"]["id"]})

        # A file with a material and a picture: the material and the size of the texture come back.
        fixtures = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tests", "data", "import")
        textured = server.data("import_file", {"path": os.path.join(fixtures, "textured-quad.glb")})
        check("1 decoded" in textured["summary"], "import_file says the picture was decoded")
        materials = server.data("geometry_summary", {"node": textured["node"]["name"]})["materials"]
        check(len(materials) == 1 and materials[0]["name"] == "Painted", "geometry_summary lists the material")
        check(materials[0]["has_texture"] and materials[0]["texture"]["width"] == 4 and materials[0]["texture"]["height"] == 4,
              "and the size of its texture")
        check(abs(materials[0]["base_color"][0] - 0.9) < 1e-6 and "missing_texture" not in materials[0],
              "and its base colour, with nothing missing")
        check(server.data("geometry_summary", {"node": "box"})["materials"] == [], "a node with no material lists none")
        shown = render(server, node=textured["node"]["name"], width=160, height=160, azimuth=0, elevation=0)[1]
        hidden = render(server, node=textured["node"]["name"], width=160, height=160, azimuth=0, elevation=0,
                        materials=False)[1]
        check(png_size(shown) == (160, 160) and shown != hidden, "render_view draws materials, and materials false changes the picture")
        check(render(server, node=textured["node"]["name"], width=160, height=160, azimuth=0, elevation=0,
                     materials=True)[1] == shown, "materials true is the default")
        check(server.call("render_view", {"materials": "yes"})["isError"] is True, "a materials value that is not a boolean is an error")
        server.data("delete_node", {"node": textured["node"]["id"]})

        path = os.path.join(folder, "smoke.kut")
        before = sorted(n["name"] for n in server.data("scene_info")["nodes"])
        server.data("save_scene", {"path": path})
        check(os.path.getsize(path) > 0 and server.data("scene_info")["modified"] is False, "save_scene writes the file")

        server.data("set_parameter", {"node": "box", "parameter": "size", "value": 1, "component": 1})
        refused = server.call("new_scene")
        check(refused["isError"] is True and "discard_changes" in refused["content"][0]["text"],
              "new_scene refuses while modified, and says why")
        check(server.call("open_scene", {"path": path})["isError"] is True, "open_scene refuses while modified")
        check(len(server.data("scene_info")["nodes"]) == len(before), "a refused call changes nothing")
        server.data("open_scene", {"path": path, "discard_changes": True})
        after = server.data("scene_info")
        check(sorted(n["name"] for n in after["nodes"]) == before and after["display_node"]["name"] == "box",
              "open_scene restores the nodes and the display node")
        check(server.data("geometry_summary")["bounds"]["size"] == [8.0, 4.0, 6.0], "and the parameters")
        server.data("new_scene")
        check(server.data("scene_info")["nodes"] == [], "new_scene empties an unmodified scene")

        # Rendering a node of a scene that shows none leaves it showing none.
        server.data("create_node", {"type": "helix::cube", "name": "lonely"})
        _, lonely = render(server, node="lonely", width=64, height=64)
        check(png_size(lonely) == (64, 64) and server.data("scene_info")["display_node"] is None,
              "render_view of a node with no display node set puts back no display node")

    # Failures are results, not protocol errors.
    unknown = server.call("geometry_summary", {"node": "ghost"})
    check(unknown["isError"] is True and "ghost" in unknown["content"][0]["text"], "an unknown node is isError with a message")
    check(server.call("create_node", {"type": "helix::nothing"})["isError"] is True, "an unknown node type is isError")
    check(server.call("set_parameter", {"node": "ghost", "parameter": "x", "value": 1})["isError"] is True,
          "set_parameter on an unknown node is isError")
    check(server.call("import_file", {"path": "/nonexistent/file.xyz"})["isError"] is True, "import_file of a bad file is isError")

    # Protocol errors.
    check(server.request("no/such/method")["error"]["code"] == -32601, "an unknown method is -32601")
    check(server.request("tools/call", {"name": "no_such_tool", "arguments": {}})["error"]["code"] == -32602,
          "an unknown tool is -32602")
    check(server.request("tools/call", {"arguments": {}})["error"]["code"] == -32602, "a call without a name is -32602")
    server.send_raw("{this is not json")
    reply = server.receive()
    check(reply["error"]["code"] == -32700 and reply["id"] is None, "invalid JSON is -32700")
    server.notify("notifications/whatever")
    check(server.request("ping")["result"] == {}, "a notification gets no reply")

    # Standard output is the protocol and nothing else.
    check(all(json.loads(line).get("jsonrpc") == "2.0" for line in server.raw), "every line of stdout is JSON-RPC")

    # End of input ends the process.
    server.process.stdin.close()
    try:
        code = server.process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        server.process.kill()
        code = None
    check(code == 0, "end of input exits with 0 (got %r)" % (code,))
    check(all(json.loads(line).get("jsonrpc") == "2.0" for line in server.raw), "nothing leaked to stdout at exit")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: python3 -I mcp-smoke.py <path to the Helix binary>")
    try:
        main(sys.argv[1])
    except Exception as error:  # a crash of the script or of the server is a failure
        print("FAIL  %s: %s" % (type(error).__name__, error))
        failures.append(str(error))
    print("\n%s" % ("all checks passed" if not failures else "%d check(s) failed" % len(failures)))
    sys.exit(1 if failures else 0)
