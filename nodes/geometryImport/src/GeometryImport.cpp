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
    // reporting an error when there is nothing readable to fill it from.
    void readFileInto(helix::NodePacket& packet);
};

helix::import::UpAxis upAxisFrom(const std::string& value)
{
    if (value == "y") return helix::import::UpAxis::Y;
    if (value == "z") return helix::import::UpAxis::Z;
    return helix::import::UpAxis::Auto;
}

void GeometryImport::readFileInto(helix::NodePacket& packet)
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
}

void GeometryImport::cook()
{
    using namespace helix;

    if (!outputRequested(0)) return;

    NodePacket packet;
    readFileInto(packet);
    // Nothing read, because the path is unset or the file failed: the output is an empty mesh,
    // so whatever sits downstream still has geometry of the right kind to work on.
    if (packet.size() == 0) packet.addPrimitive(std::make_shared<geo::Mesh>());
    setOutputPacket(0, packet);
}

} // namespace

HELIX_REGISTER_NODE(geometryImport, GeometryImport)
