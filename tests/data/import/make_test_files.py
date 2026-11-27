#!/usr/bin/env python3
"""Writes the small hand made test files of the import tests, with the standard library only.

    python3 make_test_files.py [output folder]

The files Blender has to make (the Draco compressed glb, the fbx and the blend) come from
make_blender_files.py, run by make-test-files.sh.
"""
import base64
import json
import os
import struct
import sys
import zipfile

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))


def pad4(data, fill=b"\x00"):
    return data + fill * (-len(data) % 4)


# A cube of side 2 centred on the origin, with 4 corners per face so every face has its own
# normal and its own UV square: 24 vertices and 12 triangles.
def cube_arrays():
    faces = [
        ((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),
        ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),
        ((1, 0, 0), [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]),
        ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),
        ((0, 1, 0), [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)]),
        ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)]),
    ]
    uvs = [(0, 0), (1, 0), (1, 1), (0, 1)]
    positions, normals, texcoords, indices = [], [], [], []
    for normal, corners in faces:
        base = len(positions)
        for corner, uv in zip(corners, uvs):
            positions.append(corner)
            normals.append(normal)
            texcoords.append(uv)
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, texcoords, indices


def pack_floats(rows):
    return b"".join(struct.pack("<%df" % len(r), *r) for r in rows)


def bounds(rows):
    return [min(r[i] for r in rows) for i in range(3)], [max(r[i] for r in rows) for i in range(3)]


def glb(gltf, binary):
    json_bytes = pad4(json.dumps(gltf, separators=(",", ":")).encode(), b" ")
    binary = pad4(binary)
    total = 12 + 8 + len(json_bytes) + 8 + len(binary)
    return (
        struct.pack("<III", 0x46546C67, 2, total)
        + struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes
        + struct.pack("<II", len(binary), 0x004E4942) + binary
    )


def mesh_gltf(positions, normals, texcoords, indices, nodes, scene_nodes, with_material=True):
    pos_bytes = pack_floats(positions)
    nor_bytes = pack_floats(normals) if normals else b""
    uv_bytes = pack_floats(texcoords) if texcoords else b""
    idx_bytes = pad4(struct.pack("<%dH" % len(indices), *indices))
    binary = pos_bytes + nor_bytes + uv_bytes + idx_bytes
    views, accessors, attributes = [], [], {}
    offset = 0

    def add(data_len, target, component, count, kind, extra=None):
        nonlocal offset
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": data_len, "target": target})
        accessor = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind}
        accessor.update(extra or {})
        accessors.append(accessor)
        offset += data_len
        return len(accessors) - 1

    lo, hi = bounds(positions)
    attributes["POSITION"] = add(len(pos_bytes), 34962, 5126, len(positions), "VEC3", {"min": lo, "max": hi})
    if normals:
        attributes["NORMAL"] = add(len(nor_bytes), 34962, 5126, len(normals), "VEC3")
    if texcoords:
        attributes["TEXCOORD_0"] = add(len(uv_bytes), 34962, 5126, len(texcoords), "VEC2")
    index_accessor = add(len(idx_bytes), 34963, 5123, len(indices), "SCALAR")
    primitive = {"attributes": attributes, "indices": index_accessor, "mode": 4}
    gltf = {
        "asset": {"version": "2.0", "generator": "helix test files"},
        "scene": 0,
        "scenes": [{"nodes": scene_nodes}],
        "nodes": nodes,
        "meshes": [{"name": "Mesh", "primitives": [primitive]}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    if with_material:
        primitive["material"] = 0
        gltf["materials"] = [
            {"name": "Red", "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.1, 0.1, 1.0]}}
        ]
    return gltf, binary


def write(name, data):
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(os.path.join(OUT, name), mode) as f:
        f.write(data)


def main():
    positions, normals, texcoords, indices = cube_arrays()

    # A single triangle, as a .gltf whose buffer is embedded as base64.
    tri_gltf, tri_bin = mesh_gltf(
        [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [], [0, 1, 2], [{"mesh": 0, "name": "Triangle"}], [0],
        with_material=False,
    )
    tri_gltf["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(pad4(tri_bin)).decode()
    write("triangle.gltf", json.dumps(tri_gltf, indent=1))

    # The cube with UVs and normals.
    gltf, binary = mesh_gltf(positions, normals, texcoords, indices, [{"mesh": 0, "name": "Cube"}], [0])
    write("cube.glb", glb(gltf, binary))

    # Two nodes: a parent moved 10 along x, and its child, moved 5 along y and doubled in size.
    # Both draw the same cube. World bounds: the parent x 9..11, the child x 8..12, y 3..7.
    nodes = [
        {"name": "Parent", "mesh": 0, "translation": [10, 0, 0], "children": [1]},
        {"name": "Child", "mesh": 0, "translation": [0, 5, 0], "scale": [2, 2, 2]},
    ]
    gltf, binary = mesh_gltf(positions, normals, texcoords, indices, nodes, [0])
    write("hierarchy.glb", glb(gltf, binary))

    # A box 2 x 2 x 4 as an ascii stl. The long side runs along z, which is how stl files are
    # written, so a Z up import stands it on its end.
    corners = [(x, y, z) for z in (0, 4) for y in (-1, 1) for x in (-1, 1)]
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    lines = ["solid box"]
    for a, b, c, d in quads:
        for tri in ((a, b, c), (a, c, d)):
            lines.append(" facet normal 0 0 0\n  outer loop")
            for v in tri:
                lines.append("   vertex %g %g %g" % corners[v])
            lines.append("  endloop\n endfacet")
    lines.append("endsolid box")
    write("box.stl", "\n".join(lines) + "\n")

    # A quad with a colour on every corner, which must stay one quad.
    write(
        "quad.ply",
        "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\n"
        "property uchar red\nproperty uchar green\nproperty uchar blue\nelement face 1\n"
        "property list uchar int vertex_indices\nend_header\n"
        "0 0 0 255 0 0\n1 0 0 0 255 0\n1 1 0 0 0 255\n0 1 0 255 255 255\n4 0 1 2 3\n",
    )

    # An obj with texture coordinates: a square, one quad.
    write(
        "uvsquare.obj",
        "# a square with texture coordinates\nv 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1 4/4/1\n",
    )

    # USD, as text and as a usdz archive (stored, with every file aligned on 64 bytes).
    usda = (
        '#usda 1.0\n(\n    defaultPrim = "Root"\n    upAxis = "Y"\n    metersPerUnit = 1\n)\n\n'
        'def Xform "Root"\n{\n    double3 xformOp:translate = (0, 3, 0)\n'
        '    uniform token[] xformOpOrder = ["xformOp:translate"]\n\n'
        '    def Mesh "Quad"\n    {\n'
        "        int[] faceVertexCounts = [4]\n"
        "        int[] faceVertexIndices = [0, 1, 2, 3]\n"
        "        point3f[] points = [(0, 0, 0), (2, 0, 0), (2, 0, 2), (0, 0, 2)]\n"
        "        normal3f[] normals = [(0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 1, 0)] (interpolation = \"vertex\")\n"
        "        texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1)] (interpolation = \"vertex\")\n"
        "    }\n}\n"
    )
    write("quad.usda", usda)
    write_usdz("quad.usdz", "quad.usda", usda.encode())


def write_usdz(name, inner, data):
    path = os.path.join(OUT, name)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        info = zipfile.ZipInfo(inner, date_time=(2024, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_STORED
        # The data of an entry has to start on a 64 byte boundary, which the extra field pads to.
        header = 30 + len(inner.encode())
        pad = (-(archive.fp.tell() + header + 4)) % 64
        info.extra = struct.pack("<HH", 0x1986, pad) + b"\x00" * pad
        archive.writestr(info, data)


main()
