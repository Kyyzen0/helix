#include "Engine/GeometryAlgorithms/ObjReader.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Core/Types.h"
#include "Engine/Primitives/Mesh.h"
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <string>
#include <vector>

namespace helix::utils {

namespace {

// Splits an obj line on runs of whitespace, dropping the empty fields that
// leading or doubled spaces would otherwise produce.
std::vector<std::string> splitFields(const std::string& line)
{
    std::vector<std::string> fields;
    boost::split(fields, line, boost::is_any_of(" \t\r"), boost::token_compress_on);
    fields.erase(
        std::remove_if(
            fields.begin(),
            fields.end(),
            [](const std::string& field) { return field.empty(); }
        ),
        fields.end()
    );
    return fields;
}

// Resolves one vertex reference of a face to a point offset.
//
//     "12"      -> 11
//     "12/4/7"  -> 11
//     "-1"      -> the point added most recently
//
// Obj counts vertices from one and lets a negative index count back from the
// end, so both forms land on the same zero based offset the mesh uses.
// Returns false when the field is not a number at all.
bool getPointOffset(const std::string& field, Offset pointCount, Offset& pointOffset)
{
    const std::string indexText = field.substr(0, field.find('/'));

    int objIndex = 0;
    try
    {
        objIndex = std::stoi(indexText);
    }
    catch (const std::exception&)
    {
        return false;
    }

    if (objIndex > 0 && static_cast<Offset>(objIndex) <= pointCount)
    {
        pointOffset = objIndex - 1;
        return true;
    }
    if (objIndex < 0 && static_cast<Offset>(-objIndex) <= pointCount)
    {
        pointOffset = pointCount - static_cast<Offset>(-objIndex);
        return true;
    }
    return false;
}

// Resolves the texture coordinate of one vertex reference ("12/4/7" names the fourth one), or
// returns false when the reference names none.
bool getTexcoordOffset(const std::string& field, size_t texcoordCount, size_t& texcoordOffset)
{
    const size_t firstSlash = field.find('/');
    if (firstSlash == std::string::npos) return false;
    const size_t secondSlash = field.find('/', firstSlash + 1);
    const std::string indexText = field.substr(firstSlash + 1, secondSlash - firstSlash - 1);
    if (indexText.empty()) return false;

    int objIndex = 0;
    try
    {
        objIndex = std::stoi(indexText);
    }
    catch (const std::exception&)
    {
        return false;
    }
    if (objIndex > 0 && static_cast<size_t>(objIndex) <= texcoordCount)
    {
        texcoordOffset = objIndex - 1;
        return true;
    }
    if (objIndex < 0 && static_cast<size_t>(-objIndex) <= texcoordCount)
    {
        texcoordOffset = texcoordCount - static_cast<size_t>(-objIndex);
        return true;
    }
    return false;
}

} // namespace

void readObjInto(std::istream& file, geo::Mesh& mesh, bool readTexcoords)
{
    std::vector<Vector3> texcoords;
    // One texture coordinate per corner of the faces added, in the order the mesh numbers its
    // vertices, and whether any corner named one.
    std::vector<Vector3> cornerTexcoords;
    bool anyTexcoord = false;

    std::string line;
    while (std::getline(file, line))
    {
        const std::vector<std::string> fields = splitFields(line);
        if (fields.empty()) continue;

        const std::string& record = fields.front();

        if (record == "v" && fields.size() >= 4)
        {
            mesh.addPoint(Vector3(std::stof(fields[1]), std::stof(fields[2]), std::stof(fields[3])));
        }
        else if (readTexcoords && record == "vt" && fields.size() >= 3)
        {
            texcoords.emplace_back(std::stof(fields[1]), std::stof(fields[2]), 0.f);
        }
        else if (record == "f" || record == "l")
        {
            std::vector<Offset> pointOffsets;
            pointOffsets.reserve(fields.size() - 1);
            std::vector<Vector3> faceTexcoords;

            const Offset pointCount = mesh.getNumPoints();
            for (size_t field = 1; field < fields.size(); ++field)
            {
                Offset pointOffset = 0;
                if (getPointOffset(fields[field], pointCount, pointOffset))
                {
                    pointOffsets.push_back(pointOffset);
                    if (readTexcoords)
                    {
                        size_t texcoordOffset = 0;
                        const bool named =
                            getTexcoordOffset(fields[field], texcoords.size(), texcoordOffset);
                        anyTexcoord = anyTexcoord || named;
                        faceTexcoords.push_back(named ? texcoords[texcoordOffset] : Vector3::Zero());
                    }
                }
            }

            if (pointOffsets.size() >= 2)
            {
                mesh.addFace(pointOffsets, record == "f");
                cornerTexcoords.insert(cornerTexcoords.end(), faceTexcoords.begin(), faceTexcoords.end());
            }
        }
    }

    if (readTexcoords && anyTexcoord)
    {
        auto uvs = mesh.addAttribute<Vector3>(attr::AttributeOwner::VERTEX, attr::names::uv);
        for (size_t corner = 0; corner < cornerTexcoords.size(); ++corner)
            uvs.setValue(corner, cornerTexcoords[corner]);
    }
}

} // namespace helix::utils
