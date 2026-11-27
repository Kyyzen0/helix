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
import zlib

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


def png(width, height, pixel):
    """An 8 bit RGBA PNG made with zlib, `pixel(x, y)` giving the (r, g, b, a) of the column x and the row y from the top."""
    def chunk(kind, body):
        crc = zlib.crc32(kind + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)

    rows = b"".join(b"\x00" + bytes(c for x in range(width) for c in pixel(x, y)) for y in range(height))
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 9))
        + chunk(b"IEND", b"")
    )


# Four 2 x 2 quadrants: red at the top left, green at the top right, blue at the bottom left, white
# at the bottom right. Row 0 is the top of the picture.
def quadrants_png():
    colours = {(0, 0): (255, 0, 0, 255), (1, 0): (0, 255, 0, 255), (0, 1): (0, 0, 255, 255), (1, 1): (255, 255, 255, 255)}
    return png(4, 4, lambda x, y: colours[(x // 2, y // 2)])


# A quad of side 2 on the z = 0 plane, its UVs at the corners as glTF writes them: the origin is the
# top left of the picture, so the corner at the bottom left of the quad has v = 1.
QUAD_POSITIONS = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
QUAD_NORMALS = [(0, 0, 1)] * 4
QUAD_UVS = [(0, 1), (1, 1), (1, 0), (0, 0)]
QUAD_INDICES = [0, 1, 2, 0, 2, 3]


def quad_gltf(quads, materials, image=None, image_uri=None):
    """A glTF with one mesh per entry of `quads` (a name, a material index and a translation each).

    With `image` (PNG bytes) the picture goes into the binary buffer, and with `image_uri` the glTF
    points at a file of that name. Returns the document and the binary buffer.
    """
    binary = b""
    views, accessors, meshes, nodes = [], [], [], []

    def add(data, target, component, count, kind, extra=None):
        nonlocal binary
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data), **({"target": target} if target else {})})
        accessors.append({"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind, **(extra or {})})
        binary += pad4(data)
        return len(accessors) - 1

    lo, hi = bounds(QUAD_POSITIONS)
    for index, (name, material, translation) in enumerate(quads):
        attributes = {
            "POSITION": add(pack_floats(QUAD_POSITIONS), 34962, 5126, 4, "VEC3", {"min": lo, "max": hi}),
            "NORMAL": add(pack_floats(QUAD_NORMALS), 34962, 5126, 4, "VEC3"),
            "TEXCOORD_0": add(pack_floats(QUAD_UVS), 34962, 5126, 4, "VEC2"),
        }
        indices = add(struct.pack("<6H", *QUAD_INDICES), 34963, 5123, 6, "SCALAR")
        meshes.append({"name": name, "primitives": [{"attributes": attributes, "indices": indices, "mode": 4, "material": material}]})
        nodes.append({"name": name, "mesh": index, "translation": translation})

    gltf = {
        "asset": {"version": "2.0", "generator": "helix test files"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "accessors": accessors,
    }
    if image is not None:
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(image)})
        gltf["images"] = [{"bufferView": len(views) - 1, "mimeType": "image/png"}]
        binary += pad4(image)
    elif image_uri is not None:
        gltf["images"] = [{"uri": image_uri}]
    if image is not None or image_uri is not None:
        gltf["textures"] = [{"source": 0}]
    gltf["bufferViews"] = views
    gltf["buffers"] = [{"byteLength": len(binary)}]
    return gltf, binary


def skinned_arm_gltf(meshes, armature=None, armature_lift=None):
    """A two joint arm with meshes skinned to it, and two clips that bend its elbow.

    The joints are Root at the origin and Child one unit above it (a glTF node each), with the inverse
    bind matrices of their rest pose. Every entry of `meshes` is a tube of four columns along y from 0
    to 2 (three rows of four vertices), described by a (name, x offset, skinned, node translation)
    tuple. A skinned tube is weighted to the joints: the row at y = 0 wholly to Root, the row at y = 1
    half to each, and the row at y = 2 wholly to Child. The clips are "Bend" (Child turns from 0 to 90
    degrees about z over one second, linear) and "Snap" (the same two keys, stepped, so it holds 0
    degrees and jumps to 90 at t = 1).

    With `armature` (a node dict, translation and rotation) the joints hang under a node of that
    transform, as the armature object of a Blender export does, and the inverse bind matrices follow.
    The tubes are then drawn in the world position of the bind pose, around the Root joint, which is
    where the file's own x offsets already put them.

    A mesh tuple may carry a fifth item, a lift: the tube is then authored that many units up and has a
    skin of its own (the same two joints) whose inverse bind matrices take that into account, as the
    ones of a mesh whose node was moved do. Skinned, it lands on the arm again, `lift` below where its
    vertices are. With `armature_lift` (a vector) and an armature, a third clip "Lift" moves the
    armature node itself from its translation to that translation plus the vector over one second.
    """
    import math

    def mat_mul(a, b):
        return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]

    def translation(x, y, z):
        return [[1, 0, 0, x], [0, 1, 0, y], [0, 0, 1, z], [0, 0, 0, 1]]

    def rotation_y(degrees):
        c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
        return [[c, 0, s, 0], [0, 1, 0, 0], [-s, 0, c, 0], [0, 0, 0, 1]]

    def invert_rigid(m):
        # The inverse of a rotation and a translation: the transposed rotation and the rotated minus translation.
        r = [[m[c][r_] for c in range(3)] for r_ in range(3)]
        t = [-sum(r[i][k] * m[k][3] for k in range(3)) for i in range(3)]
        return [r[0] + [t[0]], r[1] + [t[1]], r[2] + [t[2]], [0, 0, 0, 1]]

    def column_major(m):
        return [m[r][c] for c in range(4) for r in range(4)]

    nodes = []
    scene_roots = []
    above = [[1 if r == c else 0 for c in range(4)] for r in range(4)]
    root_index = 0
    if armature:
        nodes.append(dict(name="Armature", **armature))
        root_index = 1
        # The armature is a translation and, optionally, a turn about y, whichever the file lists.
        above = mat_mul(translation(*armature.get("translation", [0, 0, 0])),
                        rotation_y(armature.get("rotation_y_degrees", 0)))
        nodes[0].pop("rotation_y_degrees", None)
        if "rotation_y_degrees" in armature:
            half = math.radians(armature["rotation_y_degrees"]) / 2
            nodes[0]["rotation"] = [0, math.sin(half), 0, math.cos(half)]
        nodes[0]["children"] = [1]
    nodes.append({"name": "Root", "children": [root_index + 1]})
    nodes.append({"name": "Child", "translation": [0, 1, 0]})
    scene_roots.append(0)
    joint_nodes = [root_index, root_index + 1]
    root_world = above
    child_world = mat_mul(above, translation(0, 1, 0))
    ibm = column_major(invert_rigid(root_world)) + column_major(invert_rigid(child_world))

    def lifted_ibm(lift):
        down = translation(0, -lift, 0)
        return column_major(mat_mul(invert_rigid(root_world), down)) + column_major(mat_mul(invert_rigid(child_world), down))

    lifts = sorted({m[4] if len(m) > 4 else 0 for m in meshes})

    binary = b""
    views, accessors, mesh_defs = [], [], []

    def add(data, target, component, count, kind, extra=None):
        nonlocal binary
        view = {"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        accessors.append({"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind, **(extra or {})})
        binary += pad4(data)
        return len(accessors) - 1

    # Rows of the tube and the weights each row gets.
    rows = [(0.0, (1.0, 0.0)), (1.0, (0.5, 0.5)), (2.0, (0.0, 1.0))]
    columns = [(-0.5, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5)]
    indices = []
    for row in range(2):
        for column in range(4):
            a = row * 4 + column
            b = row * 4 + (column + 1) % 4
            indices += [a, b, b + 4, a, b + 4, a + 4]

    for mesh_index, (name, x_offset, skinned, node_translation, *rest) in enumerate(meshes):
        lift = rest[0] if rest else 0
        positions, normals, joints, weights = [], [], [], []
        for y, (w0, w1) in rows:
            for x, z in columns:
                positions.append((x + x_offset, y + lift, z))
                length = math.hypot(x, z)
                normals.append((x / length, 0.0, z / length))
                joints.append((0, 1, 0, 0))
                weights.append((w0, w1, 0.0, 0.0))
        lo, hi = bounds(positions)
        attributes = {
            "POSITION": add(pack_floats(positions), 34962, 5126, len(positions), "VEC3", {"min": lo, "max": hi}),
            "NORMAL": add(pack_floats(normals), 34962, 5126, len(normals), "VEC3"),
        }
        if skinned:
            attributes["JOINTS_0"] = add(b"".join(struct.pack("<4B", *j) for j in joints), 34962, 5121, len(joints), "VEC4")
            attributes["WEIGHTS_0"] = add(pack_floats(weights), 34962, 5126, len(weights), "VEC4")
        index_accessor = add(struct.pack("<%dH" % len(indices), *indices), 34963, 5123, len(indices), "SCALAR")
        mesh_defs.append({"name": name, "primitives": [{"attributes": attributes, "indices": index_accessor, "mode": 4}]})
        node = {"name": name, "mesh": mesh_index}
        if skinned:
            node["skin"] = lifts.index(lift)
        if node_translation:
            node["translation"] = node_translation
        scene_roots.append(len(nodes))
        nodes.append(node)

    ibm_accessor = add(pack_floats([ibm[:16], ibm[16:]]), None, 5126, 2, "MAT4")
    times = add(pack_floats([(0.0,), (1.0,)]), None, 5126, 2, "SCALAR", {"min": [0.0], "max": [1.0]})
    half = math.radians(90) / 2
    rotations = add(pack_floats([(0.0, 0.0, 0.0, 1.0), (0.0, 0.0, math.sin(half), math.cos(half))]), None, 5126, 2, "VEC4")
    child_node = joint_nodes[1]
    skins = [
        {"inverseBindMatrices": ibm_accessor, "joints": joint_nodes, "skeleton": root_index}
    ]
    if lifts != [0]:
        skins = []
        for lift in lifts:
            matrices = ibm if lift == 0 else lifted_ibm(lift)
            skins.append(
                {
                    "inverseBindMatrices": ibm_accessor if lift == 0 else add(pack_floats([matrices[:16], matrices[16:]]), None, 5126, 2, "MAT4"),
                    "joints": joint_nodes,
                    "skeleton": root_index,
                }
            )
    lift_animation = []
    if armature and armature_lift:
        base = armature.get("translation", [0, 0, 0])
        moved = [base[i] + armature_lift[i] for i in range(3)]
        positions_accessor = add(pack_floats([tuple(base), tuple(moved)]), None, 5126, 2, "VEC3")
        lift_animation = [
            {
                "name": "Lift",
                "samplers": [{"input": times, "output": positions_accessor, "interpolation": "LINEAR"}],
                "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}],
            }
        ]
    gltf = {
        "asset": {"version": "2.0", "generator": "helix test files"},
        "scene": 0,
        "scenes": [{"nodes": scene_roots}],
        "nodes": nodes,
        "meshes": mesh_defs,
        "skins": skins,
        "animations": [
            {
                "name": "Bend",
                "samplers": [{"input": times, "output": rotations, "interpolation": "LINEAR"}],
                "channels": [{"sampler": 0, "target": {"node": child_node, "path": "rotation"}}],
            },
            {
                "name": "Snap",
                "samplers": [{"input": times, "output": rotations, "interpolation": "STEP"}],
                "channels": [{"sampler": 0, "target": {"node": child_node, "path": "rotation"}}],
            },
        ]
        + lift_animation,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
    }
    return gltf, binary


def six_influence_gltf():
    """A triangle skinned to a chain of six joints, to see how weights are reduced to four.

    The joints stand one above the other, joint k at y = k. The first vertex is pulled by all six
    (0.35, 0.25, 0.15, 0.10, 0.09 and 0.06), the second by joint 3 alone with a weight of 0.5 that does
    not sum to one, and the third has no weight at all.
    """
    nodes = [{"name": "Joint0", "children": [1]}]
    for k in range(1, 6):
        node = {"name": "Joint%d" % k, "translation": [0, 1, 0]}
        if k < 5:
            node["children"] = [k + 1]
        nodes.append(node)
    nodes.append({"name": "Triangle", "mesh": 0, "skin": 0})

    positions = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    sets = [
        ([0, 1, 2, 3], [0.35, 0.25, 0.15, 0.10]),
        ([4, 5, 0, 0], [0.09, 0.06, 0.0, 0.0]),
    ]
    second_vertex = ([3, 0, 0, 0], [0.5, 0.0, 0.0, 0.0])
    third_vertex = ([0, 0, 0, 0], [0.0, 0.0, 0.0, 0.0])
    joints0 = [sets[0][0], second_vertex[0], third_vertex[0]]
    weights0 = [sets[0][1], second_vertex[1], third_vertex[1]]
    joints1 = [sets[1][0], [0, 0, 0, 0], [0, 0, 0, 0]]
    weights1 = [sets[1][1], [0.0] * 4, [0.0] * 4]

    binary = b""
    views, accessors = [], []

    def add(data, target, component, count, kind, extra=None):
        nonlocal binary
        view = {"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        accessors.append({"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind, **(extra or {})})
        binary += pad4(data)
        return len(accessors) - 1

    lo, hi = bounds(positions)
    attributes = {
        "POSITION": add(pack_floats(positions), 34962, 5126, 3, "VEC3", {"min": lo, "max": hi}),
        "JOINTS_0": add(b"".join(struct.pack("<4B", *j) for j in joints0), 34962, 5121, 3, "VEC4"),
        "WEIGHTS_0": add(pack_floats(weights0), 34962, 5126, 3, "VEC4"),
        "JOINTS_1": add(b"".join(struct.pack("<4B", *j) for j in joints1), 34962, 5121, 3, "VEC4"),
        "WEIGHTS_1": add(pack_floats(weights1), 34962, 5126, 3, "VEC4"),
    }
    indices = add(struct.pack("<3H", 0, 1, 2), 34963, 5123, 3, "SCALAR")
    ibm = []
    for k in range(6):
        ibm.append([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -k, 0, 1])
    ibm_accessor = add(pack_floats(ibm), None, 5126, 6, "MAT4")
    gltf = {
        "asset": {"version": "2.0", "generator": "helix test files"},
        "scene": 0,
        "scenes": [{"nodes": [0, 6]}],
        "nodes": nodes,
        "meshes": [{"name": "Triangle", "primitives": [{"attributes": attributes, "indices": indices, "mode": 4}]}],
        "skins": [{"inverseBindMatrices": ibm_accessor, "joints": [0, 1, 2, 3, 4, 5], "skeleton": 0}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
    }
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

    # A quad with a base colour factor and a 4 x 4 picture of four coloured quadrants, embedded in
    # the glb, and the same quad as a gltf whose picture and buffer are files next to it.
    textured = [{"name": "Painted", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.8, 0.7, 1.0], "baseColorTexture": {"index": 0}}}]
    gltf, binary = quad_gltf([("Quad", 0, [0, 0, 0])], textured, image=quadrants_png())
    write("textured-quad.glb", glb(gltf, binary))
    gltf, binary = quad_gltf([("Quad", 0, [0, 0, 0])], textured, image_uri="quadrants.png")
    gltf["buffers"][0]["uri"] = "textured-quad.bin"
    write("textured-quad.gltf", json.dumps(gltf, indent=1))
    write("textured-quad.bin", binary)
    write("quadrants.png", quadrants_png())

    # The same picture on a usd quad, through a UsdPreviewSurface whose diffuse colour is a texture.
    # USD's texture coordinates start at the bottom left, so the corners read (0, 0) to (0, 1).
    write(
        "textured-quad.usda",
        '#usda 1.0\n(\n    defaultPrim = "Root"\n    upAxis = "Y"\n)\n\n'
        'def Xform "Root"\n{\n'
        '    def Mesh "Quad" (prepend apiSchemas = ["MaterialBindingAPI"])\n    {\n'
        "        int[] faceVertexCounts = [4]\n"
        "        int[] faceVertexIndices = [0, 1, 2, 3]\n"
        "        point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]\n"
        '        texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1)] (interpolation = "vertex")\n'
        "        rel material:binding = </Root/Paint>\n    }\n"
        '    def Material "Paint"\n    {\n'
        "        token outputs:surface.connect = </Root/Paint/Surface.outputs:surface>\n"
        '        def Shader "Surface"\n        {\n'
        '            uniform token info:id = "UsdPreviewSurface"\n'
        "            color3f inputs:diffuseColor.connect = </Root/Paint/Picture.outputs:rgb>\n"
        "            token outputs:surface\n        }\n"
        '        def Shader "Picture"\n        {\n'
        '            uniform token info:id = "UsdUVTexture"\n'
        "            asset inputs:file = @./quadrants.png@\n"
        "            float2 inputs:st.connect = </Root/Paint/Coordinates.outputs:result>\n"
        "            float3 outputs:rgb\n        }\n"
        '        def Shader "Coordinates"\n        {\n'
        '            uniform token info:id = "UsdPrimvarReader_float2"\n'
        '            string inputs:varname = "st"\n'
        "            float2 outputs:result\n        }\n    }\n}\n",
    )

    # Two meshes with two materials of their own, so a merge has to renumber the second one.
    plain = [
        {"name": "Red", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.0, 0.0, 1.0]}},
        {"name": "Blue", "pbrMetallicRoughness": {"baseColorFactor": [0.0, 0.0, 1.0, 1.0]}},
    ]
    gltf, binary = quad_gltf([("First", 0, [0, 0, 0]), ("Second", 1, [3, 0, 0])], plain)
    write("two-materials.glb", glb(gltf, binary))

    # A two joint arm with a tube skinned to it, as a glb and as a gltf with its buffer next to it. The
    # same arm with its joints under an armature node that is moved and turned, two tubes sharing the
    # one skin next to a tube that is not skinned (to see joint indices survive a merge).
    gltf, binary = skinned_arm_gltf([("Arm", 0.0, True, None)])
    write("skinned-arm.glb", glb(gltf, binary))
    gltf["buffers"][0]["uri"] = "skinned-arm.bin"
    write("skinned-arm.gltf", json.dumps(gltf, indent=1))
    write("skinned-arm.bin", binary)
    gltf, binary = skinned_arm_gltf(
        [("Arm", 5.0, True, None)], armature={"translation": [5, 0, 0], "rotation_y_degrees": 90}
    )
    write("skinned-arm-armature.glb", glb(gltf, binary))
    # The same armature with a clip that moves the armature node itself 3 up, which the joints under it follow.
    gltf, binary = skinned_arm_gltf(
        [("Arm", 5.0, True, None)], armature={"translation": [5, 0, 0], "rotation_y_degrees": 90}, armature_lift=[0, 3, 0]
    )
    write("skinned-arm-armature-lift.glb", glb(gltf, binary))
    # Two tubes skinned to the same two joints by skins with different inverse bind matrices: the second
    # tube is authored 2 up, and its matrices take it back down onto the arm.
    gltf, binary = skinned_arm_gltf([("ArmA", 0.0, True, None), ("ArmB", 3.0, True, None, 2)])
    write("skinned-two-skins.glb", glb(gltf, binary))
    # Six influences on a vertex, and weights that do not sum to one.
    gltf, binary = six_influence_gltf()
    write("skinned-six-influences.glb", glb(gltf, binary))

    # A skinned tube whose node is moved 5 up: glTF says the node of a skinned mesh does not place it, the
    # skeleton does, so the tube stays where its vertices are.
    gltf, binary = skinned_arm_gltf([("Arm", 0.0, True, [0, 5, 0])])
    write("skinned-arm-moved-node.glb", glb(gltf, binary))
    gltf, binary = skinned_arm_gltf([("ArmA", 0.0, True, None), ("ArmB", 3.0, True, None), ("Prop", 0.0, False, [0, 5, 0])])
    write("skinned-two-meshes.glb", glb(gltf, binary))

    # A picture wider than the largest texture side (8192), which a solid colour keeps tiny.
    write("wide-9000x2.png", png(9000, 2, lambda x, y: (200, 100, 50, 255)))

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
