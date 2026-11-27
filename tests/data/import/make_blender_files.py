"""Writes the three test files only Blender can make: a Draco compressed glb, an fbx and a blend.

    blender --background --factory-startup --python make_blender_files.py -- <output folder>

The cube is the one of cube.glb (side 2, with UVs and normals), built from the default cube of
Blender, which is the same size.
"""
import os
import sys

import bpy

out = sys.argv[sys.argv.index("--") + 1]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_cube_add(size=2.0)
cube = bpy.context.active_object
cube.name = "Cube"
material = bpy.data.materials.new("Red")
material.diffuse_color = (0.8, 0.1, 0.1, 1.0)
cube.data.materials.append(material)
cube.data.calc_normals_split() if hasattr(cube.data, "calc_normals_split") else None


def has(operator, name):
    return name in operator.get_rna_type().properties.keys()


gltf = bpy.ops.export_scene.gltf
options = dict(
    filepath=os.path.join(out, "cube-draco.glb"),
    export_format="GLB",
    export_draco_mesh_compression_enable=True,
    export_draco_mesh_compression_level=6,
    export_image_format="NONE",
)
gltf(**{k: v for k, v in options.items() if has(gltf, k)})
bpy.ops.export_scene.fbx(filepath=os.path.join(out, "cube.fbx"), use_selection=False)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "cube.blend"), compress=True)
