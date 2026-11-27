#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Attribute/Transform.h"
#include "Engine/Core/Types.h"
#include "Engine/Import/ImportFile.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Primitives/Mesh.h"
#include <boost/algorithm/string.hpp>
#include <filesystem>

namespace {

class GeometryImport : public helix::nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;

  private:
    // Adds the meshes of the file the parameters name to the packet, leaving it empty and
    // reporting an error when there is nothing readable to fill it from. The skeleton of the file,
    // when it has one, goes to the second packet.
    void readFileInto(helix::NodePacket& packet, helix::NodePacket& skeleton);
};

// Scales a skeleton the way the import node's size scales the geometry.
//
// The points of the meshes are multiplied by the size, so the joints and the matrices that move
// them have to live in the same scaled space: with S the uniform scale, a matrix M becomes
// S M S^-1, which keeps its rotation and multiplies its translation by the size. Then the skin
// matrix pose * inverse_bind takes a scaled point of the mesh to the scaled point it would have
// reached unscaled. The names, the clips and the base of the roots are in the table, which is
// shared and not copied, so the table records the size instead and skeletonPose applies it.
void scaleSkeleton(helix::geo::Mesh& skeleton, float size)
{
    using namespace helix;
    if (size == 1.f) return;
    for (const char* name : {attr::names::restLocal, attr::names::inverseBind, attr::names::pose})
    {
        auto matrices = attr::AttributeHandle<Matrix4>(skeleton.getAttribByName(attr::AttributeOwner::POINT, name));
        for (Offset joint = 0; joint < matrices.getSize(); ++joint)
        {
            Matrix4 matrix = matrices.getValue(joint);
            matrix.col(3).head<3>() *= size;
            matrices.setValue(joint, matrix);
        }
    }
    auto table = std::make_shared<geo::Skeleton>(*skeleton.skeleton());
    table->scale *= size;
    skeleton.setSkeleton(std::move(table));
}

helix::import::UpAxis upAxisFrom(const std::string& value)
{
    if (value == "y") return helix::import::UpAxis::Y;
    if (value == "z") return helix::import::UpAxis::Z;
    return helix::import::UpAxis::Auto;
}

void GeometryImport::readFileInto(helix::NodePacket& packet, helix::NodePacket& skeleton)
{
    using namespace helix;

    String filePath = evalParmString("filePath");
    boost::trim(filePath);

    // An unset path is the state a freshly created node is in, so it stays quiet
    // and hands out empty geometry until the user points it somewhere.
    if (filePath.empty()) return;

    import::ImportOptions options;
    options.upAxis = upAxisFrom(evalParmString("upAxis"));
    options.mergeMeshes = evalParmBool("mergeMeshes");
    options.blenderPath = evalParmString("blenderPath");
    boost::trim(options.blenderPath);

    // The file is read once and kept: a cook that follows with the same file and settings
    // reuses it, and only the size below is applied again.
    std::shared_ptr<const import::ImportedFile> imported;
    try
    {
        imported = import::importFile(std::filesystem::path(filePath), options);
    }
    catch (const std::exception& error)
    {
        throwError(error.what());
        return;
    }

    const Transform scale = Transform().scale(evalParmFloat("size"));
    for (const std::shared_ptr<const geo::Mesh>& source : imported->meshes)
    {
        // A copy shares its storage with the kept mesh until the scale writes the points.
        auto mesh = std::make_shared<geo::Mesh>(*source);
        mesh->applyTransform(scale, TransformClass::POINT);
        packet.addPrimitive(std::move(mesh));
    }

    if (imported->skeleton)
    {
        // The joints scale with the geometry they move.
        auto joints = std::make_shared<geo::Mesh>(*imported->skeleton);
        joints->applyTransform(scale, TransformClass::POINT);
        scaleSkeleton(*joints, evalParmFloat("size"));
        skeleton.addPrimitive(std::move(joints));
    }
}

void GeometryImport::cook()
{
    using namespace helix;

    if (!outputRequested(0)) return;

    NodePacket packet;
    NodePacket skeleton;
    readFileInto(packet, skeleton);
    // Nothing read, because the path is unset or the file failed: the output is an empty mesh,
    // so whatever sits downstream still has geometry of the right kind to work on. The skeleton
    // of a file that has none is empty the same way.
    if (packet.size() == 0) packet.addPrimitive(std::make_shared<geo::Mesh>());
    if (skeleton.size() == 0) skeleton.addPrimitive(std::make_shared<geo::Mesh>());
    setOutputPacket(0, packet);
    setOutputPacket(1, skeleton);
}

} // namespace

HELIX_REGISTER_NODE(geometryImport, GeometryImport)
