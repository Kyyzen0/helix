// Reads USD files (usd, usda, usdc, usdz) with Model I/O, the framework of macOS that owns the
// format. It is the one reader that needs Objective-C, so it is a file of its own.
#import <Foundation/Foundation.h>
#import <ModelIO/ModelIO.h>
#include "Engine/Import/ImageDecode.h"
#include "Engine/Import/ImportFile.h"
#include "Engine/Import/Readers.h"
#include <simd/simd.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace helix::import {

namespace {

Matrix4 toMatrix(const matrix_float4x4& m)
{
    Matrix4 matrix;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            matrix(row, column) = m.columns[column][row];
    return matrix;
}

// Reads one vertex attribute as floats, `components` per vertex, or returns an empty vector when
// the mesh has no such attribute.
std::vector<float> readAttribute(MDLMesh* mesh, NSString* name, MDLVertexFormat format, int components)
{
    MDLVertexAttributeData* data = [mesh vertexAttributeDataForAttributeNamed:name asFormat:format];
    if (!data || !data.dataStart) return {};

    const NSUInteger vertexCount = mesh.vertexCount;
    std::vector<float> values(vertexCount * components);
    const char* base = static_cast<const char*>(data.dataStart);
    for (NSUInteger v = 0; v < vertexCount; ++v)
    {
        const float* source = reinterpret_cast<const float*>(base + v * data.stride);
        for (int c = 0; c < components; ++c)
            values[v * components + c] = source[c];
    }
    return values;
}

// Adds the faces of one submesh, whatever topology it stores them in.
void appendFaces(MDLSubmesh* submesh, RawMesh& out, std::size_t& skipped)
{
    id<MDLMeshBuffer> indexBuffer = [submesh indexBufferAsIndexType:MDLIndexBitDepthUInt32];
    if (!indexBuffer) return;
    MDLMeshBufferMap* map = [indexBuffer map];
    const std::uint32_t* indices = static_cast<const std::uint32_t*>(map.bytes);
    const NSUInteger count = submesh.indexCount;

    switch (submesh.geometryType)
    {
    case MDLGeometryTypeTriangles:
        for (NSUInteger i = 0; i + 2 < count; i += 3)
        {
            out.faceCounts.push_back(3);
            out.indices.insert(out.indices.end(), indices + i, indices + i + 3);
        }
        break;
    case MDLGeometryTypeTriangleStrips:
        for (NSUInteger i = 0; i + 2 < count; ++i)
        {
            // Every other triangle of a strip is stored backward.
            const bool odd = i % 2 == 1;
            out.faceCounts.push_back(3);
            out.indices.push_back(indices[i]);
            out.indices.push_back(indices[odd ? i + 2 : i + 1]);
            out.indices.push_back(indices[odd ? i + 1 : i + 2]);
        }
        break;
    case MDLGeometryTypeQuads:
        for (NSUInteger i = 0; i + 3 < count; i += 4)
        {
            out.faceCounts.push_back(4);
            out.indices.insert(out.indices.end(), indices + i, indices + i + 4);
        }
        break;
    case MDLGeometryTypeVariableTopology:
    {
        MDLSubmeshTopology* topology = submesh.topology;
        if (!topology || !topology.faceTopology) break;
        const std::uint8_t* counts = static_cast<const std::uint8_t*>(topology.faceTopology.map.bytes);
        NSUInteger cursor = 0;
        for (NSUInteger face = 0; face < topology.faceCount; ++face)
        {
            const NSUInteger faceVertices = counts[face];
            if (cursor + faceVertices > count) break;
            if (faceVertices >= 3)
            {
                out.faceCounts.push_back(static_cast<std::uint32_t>(faceVertices));
                out.indices.insert(out.indices.end(), indices + cursor, indices + cursor + faceVertices);
            }
            cursor += faceVertices;
        }
        break;
    }
    default:
        // Points and lines are not surfaces.
        skipped += count;
        break;
    }
}

} // namespace

RawScene readWithModelIO(const std::filesystem::path& file)
{
    @autoreleasepool
    {
        try
        {
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:file.c_str()]];
            NSString* extension = [[url pathExtension] lowercaseString];
            if (![MDLAsset canImportFileExtension:extension])
                throw ImportError("This version of macOS cannot read ." +
                                  std::string([extension UTF8String]) + " files.");

            MDLAsset* asset = [[MDLAsset alloc] initWithURL:url];
            if (!asset || asset.count == 0)
                throw ImportError("The file holds no object Model I/O can read.");

            RawScene scene;
            scene.declaredUp = UpAxis::Y;
            const vector_float3 up = asset.upAxis;
            if (std::abs(up.z) > 0.5f && std::abs(up.z) > std::abs(up.y)) scene.declaredUp = UpAxis::Z;

            // The pictures already decoded, by the object that holds them or by file.
            std::map<const void*, DecodeResult> decodedByTexture;
            std::map<std::string, DecodeResult> decodedByFile;
            const std::filesystem::path folder = file.parent_path();

            // Decodes the picture of a base colour property. A picture that cannot be loaded
            // leaves the material with its factor and a note.
            auto readTexture = [&](MDLMaterialProperty* base, RawMaterial& entry) {
                const DecodeResult* result = nullptr;
                std::string what = "texture";
                if (base.type == MDLMaterialPropertyTypeTexture)
                {
                    MDLTexture* texture = base.textureSamplerValue.texture;
                    if (texture && texture.name) what = std::string("texture \"") + [texture.name UTF8String] + "\"";
                    if (!texture)
                        entry.missingTexture = "the base colour texture has no picture";
                    else
                    {
                        auto found = decodedByTexture.find((__bridge const void*)texture);
                        if (found == decodedByTexture.end())
                        {
                            CGImageRef image = [texture imageFromTexture];
                            const std::string source =
                                file.string() + "#" + (texture.name ? std::string([texture.name UTF8String]) : "texture");
                            DecodeResult decoded;
                            if (image)
                                decoded = decodeCGImage(image, source);
                            else
                                decoded.error = "Model I/O cannot give its pixels";
                            found = decodedByTexture.emplace((__bridge const void*)texture, std::move(decoded)).first;
                        }
                        result = &found->second;
                    }
                }
                else
                {
                    // A file the property names, next to the model unless the path is absolute.
                    std::string written = base.type == MDLMaterialPropertyTypeURL
                                              ? std::string([[base.URLValue path] UTF8String] ?: "")
                                              : std::string([base.stringValue UTF8String] ?: "");
                    what = "texture \"" + written.substr(0, 200) + "\"";
                    std::filesystem::path path(written);
                    if (path.is_relative()) path = folder / path;
                    std::error_code error;
                    if (!std::filesystem::is_regular_file(path, error))
                        entry.missingTexture = what + " not found next to the model";
                    else
                    {
                        auto found = decodedByFile.find(path.string());
                        if (found == decodedByFile.end())
                            found = decodedByFile.emplace(path.string(), decodeImageFile(path)).first;
                        result = &found->second;
                    }
                }
                if (!result) return;
                if (result->image)
                    entry.texture = result->image;
                else
                    entry.missingTexture = what + " could not be loaded: " + result->error;
            };

            // One material entry per distinct material of the file, by name and base colour.
            std::map<std::string, int> materialIndex;
            auto materialOf = [&](MDLMaterial* material) -> int {
                if (!material) return -1;
                const std::string name = material.name ? std::string([material.name UTF8String]) : "";
                const auto found = materialIndex.find(name);
                if (found != materialIndex.end()) return found->second;

                RawMaterial entry;
                entry.name = name;
                MDLMaterialProperty* base = [material propertyWithSemantic:MDLMaterialSemanticBaseColor];
                if (base)
                {
                    // Model I/O gives a material to a mesh that has none, with a base colour of
                    // zero, so a black one is read as not set and stays white.
                    if (base.type == MDLMaterialPropertyTypeFloat3 || base.type == MDLMaterialPropertyTypeColor)
                    {
                        const vector_float3 color = base.float3Value;
                        if (color.x != 0.f || color.y != 0.f || color.z != 0.f)
                            entry.baseColor = {color.x, color.y, color.z, 1.f};
                    }
                    else if (base.type == MDLMaterialPropertyTypeTexture || base.type == MDLMaterialPropertyTypeString ||
                             base.type == MDLMaterialPropertyTypeURL)
                    {
                        entry.hasTexture = true;
                        readTexture(base, entry);
                    }
                }
                scene.materials.push_back(entry);
                const int index = static_cast<int>(scene.materials.size()) - 1;
                materialIndex[name] = index;
                return index;
            };

            // Visits an object and everything under it. The world transform comes from Model I/O,
            // which folds in the parents.
            std::function<void(MDLObject*)> visit = [&](MDLObject* object) {
                if ([object isKindOfClass:[MDLMesh class]])
                {
                    MDLMesh* mesh = static_cast<MDLMesh*>(object);
                    RawMesh raw;
                    raw.name = object.name ? std::string([object.name UTF8String]) : "mesh";
                    raw.positions = readAttribute(mesh, MDLVertexAttributePosition, MDLVertexFormatFloat3, 3);
                    raw.normals = readAttribute(mesh, MDLVertexAttributeNormal, MDLVertexFormatFloat3, 3);
                    raw.uvs = readAttribute(mesh, MDLVertexAttributeTextureCoordinate, MDLVertexFormatFloat2, 2);
                    raw.colors = readAttribute(mesh, MDLVertexAttributeColor, MDLVertexFormatFloat3, 3);

                    // The submeshes of an object become one mesh. A face keeps the material of
                    // its submesh only when the object has a single one, since the engine stores
                    // one material per imported mesh.
                    std::set<int> used;
                    for (MDLSubmesh* submesh in mesh.submeshes)
                    {
                        appendFaces(submesh, raw, scene.skippedPrimitiveCount);
                        used.insert(materialOf(submesh.material));
                    }
                    if (used.size() == 1) raw.material = *used.begin();

                    if (!raw.positions.empty() && !raw.faceCounts.empty())
                    {
                        RawInstance instance;
                        instance.mesh = scene.meshes.size();
                        instance.world = toMatrix([MDLTransform globalTransformWithObject:object atTime:0]);
                        scene.meshes.push_back(std::move(raw));
                        scene.instances.push_back(instance);
                    }
                }
                for (MDLObject* child in object.children.objects)
                    visit(child);
            };
            for (NSUInteger i = 0; i < asset.count; ++i)
                visit([asset objectAtIndex:i]);

            return scene;
        }
        catch (const ImportError&)
        {
            throw;
        }
        catch (NSException* exception)
        {
            throw ImportError(std::string("Model I/O could not read the file: ") +
                              [[exception reason] UTF8String]);
        }
        catch (const std::exception& error)
        {
            throw ImportError(std::string("The file could not be read: ") + error.what());
        }
    }
}

} // namespace helix::import
