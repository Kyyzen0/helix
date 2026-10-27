#include "Engine/Attribute/Transform.h"
#include "Engine/Core/Types.h"
#include "Engine/GeometryAlgorithms/MeshUtils.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Parameter/Ramp.h"
#include "Engine/Parameter/Template.h"
#include "Engine/Primitives/Mesh.h"
#include <Eigen/src/Core/Matrix.h>
#include <Eigen/src/Geometry/AngleAxis.h>
#include <cmath>
#include <numbers>
#include <span>
#include <string>

namespace {
// A profile cross section and whether its last point joins back to its first.
// A circle or square is closed, a ribbon or an open input curve is not.
struct Profile
{
    std::vector<helix::Vector3> points;
    bool closed = true;
};

// How the profile is modulated as it travels along each curve.
struct SweepSettings
{
    bool applyScale = false;
    helix::prm::Ramp scaleRamp;
    bool capEnds = false;
    std::string capGroupName;
    bool stretchTurns = true;
};

// Defined at the bottom of the file, below the node code they serve.
std::vector<helix::Vector3> buildCircleProfile(int columns);
std::vector<helix::Vector3> buildSquareProfile(int columns);
std::vector<helix::Vector3> buildRibbonProfile(int columns);
Profile buildProfileFromInput(helix::NodePacket& packet);
void sweepMesh(helix::geo::Mesh& mesh, const Profile& profile, const SweepSettings& settings);
} // namespace

namespace {

class Sweep : public helix::nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;

  private:
    Profile buildProfile();
};

void Sweep::cook()
{
    using namespace helix;

    if (!outputRequested(0)) return;

    NodePacket packet = cloneInputPacket(0);

    SweepSettings settings{
        evalParmBool("applyScale"),
        evalParmRamp("scaleRamp"),
        evalParmString("endCapType") == "single",
        {},
        evalParmBool("stretchTurns"),
    };
    if (settings.capEnds && evalParmBool("endCapGroupEnabled"))
        settings.capGroupName = evalParmString("endCapGroupName");

    if (evalParmString("profileShape") == "input" && !hasInput(1))
    {
        throwError("Sweep needs a profile shape wired into the second input.");
        return;
    }
    const Profile profile = buildProfile();
    if (profile.points.size() < 2)
    {
        throwError("Sweep needs a profile of at least two points.");
        return;
    }

    for (auto prim : packet.getPrimitives())
    {
        std::shared_ptr<geo::Mesh> mesh = std::dynamic_pointer_cast<geo::Mesh>(prim);
        if (!mesh) continue;

        sweepMesh(*mesh, profile, settings);
    }

    setOutputPacket(0, packet);
}

/**
 * @brief Builds the profile selected by the node's parameters.
 *
 * Each procedural shape reads its own column count. The second input option
 * takes its shape and open or closed state from the wired in curve.
 */
Profile Sweep::buildProfile()
{
    using namespace helix;

    const std::string profileShape = evalParmString("profileShape");

    Profile profile;
    if (profileShape == "square")
        profile = {buildSquareProfile(evalParmInt("squareColumns")), true};
    else if (profileShape == "ribbon")
        profile = {buildRibbonProfile(evalParmInt("ribbonColumns")), false};
    else if (profileShape == "input")
    {
        NodePacket profilePacket = cloneInputPacket(1);
        profile = buildProfileFromInput(profilePacket);
    }
    else
        profile = {buildCircleProfile(evalParmInt("roundColumns")), true};

    // Profile Scale sizes the resting cross section.
    const float profileScale = evalParmFloat("profileScale");
    for (Vector3& point : profile.points)
        point *= profileScale;

    return profile;
}

using namespace helix;

/// @brief Returns the direction of a vector, or the fallback when it has no length.
Vector3 safeNormalized(const Vector3& vector, const Vector3& fallback)
{
    const float length = vector.norm();
    if (length < 1e-9f) return fallback;
    return vector / length;
}

/// @brief Turns a vector about an axis by an angle.
Vector3 rotateVector(const Vector3& vector, const Vector3& axis, float angle)
{
    const float cosAngle = std::cos(angle);
    const float sinAngle = std::sin(angle);
    return vector * cosAngle + axis.cross(vector) * sinAngle +
           axis * (axis.dot(vector) * (1.0f - cosAngle));
}

/**
 * @brief The directions a curve runs in along its length.
 *
 * The tangent at a point is the average of the directions the curve arrives and
 * leaves in, which is the direction the profile faces. The across direction is
 * square to the tangent and is carried along the curve, so the profile keeps a
 * steady roll instead of spinning as the curve turns.
 */
struct CurveFrame
{
    std::vector<Vector3> tangents;
    std::vector<Vector3> onward;
    std::vector<Vector3> across;
};

/**
 * @brief Builds the frame the profile rides in along one curve.
 *
 * A curve that has a plane of its own starts with the across direction in that
 * plane, so a planar curve keeps a flat profile flat. A straight curve has no
 * plane, so the world axis least in line with its tangent sets the roll.
 */
CurveFrame buildCurveFrame(const geo::Mesh& mesh, Offset curveFace)
{
    const std::span<const intT> facePoints = mesh.getFacePoints(curveFace);
    const int pointCount = static_cast<int>(facePoints.size());
    const bool closed = mesh.isClosed(curveFace);

    std::vector<Vector3> positions;
    positions.reserve(pointCount);
    for (intT pointOffset : facePoints) positions.push_back(mesh.getPointPos(pointOffset));

    CurveFrame frame;
    frame.tangents.reserve(pointCount);
    frame.onward.reserve(pointCount);
    frame.across.reserve(pointCount);

    // The direction the curve leaves each point in. An open end has nothing
    // ahead of it, so it reuses the segment arriving into it.
    for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex)
    {
        if (pointIndex + 1 < pointCount)
            frame.onward.push_back(safeNormalized(positions[pointIndex + 1] - positions[pointIndex],
                                                  Vector3(0, 0, 1)));
        else if (closed)
            frame.onward.push_back(safeNormalized(positions[0] - positions[pointIndex], Vector3(0, 0, 1)));
        else
            frame.onward.push_back(safeNormalized(positions[pointIndex] - positions[pointIndex - 1],
                                                  Vector3(0, 0, 1)));
    }

    for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex)
    {
        const Vector3 incoming =
            pointIndex > 0 ? safeNormalized(positions[pointIndex] - positions[pointIndex - 1],
                                            frame.onward[pointIndex])
                            : frame.onward[0];

        // The average of the two directions faces along the turn. A curve that
        // doubles back has no average, so the onward direction stands in.
        Vector3 tangent = incoming + frame.onward[pointIndex];
        if (tangent.norm() < 1e-4f) tangent = frame.onward[pointIndex];
        frame.tangents.push_back(safeNormalized(tangent, frame.onward[pointIndex]));
    }

    // The first across direction, in the curve's own plane when it has one.
    Vector3 across = Vector3(0, 0, 0);
    const Vector3 planeNormal = utils::polygonNormal(mesh.pointPosSpan(), facePoints);
    if (planeNormal.norm() > 1e-4f) across = planeNormal.cross(frame.tangents[0]);
    if (across.norm() < 1e-6f)
    {
        const Vector3 axes[] = {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
        const Vector3& tangent = frame.tangents[0];
        const Vector3* leastAligned = &axes[0];
        float alignment = std::abs(tangent.dot(axes[0]));
        for (const Vector3& axis : axes)
        {
            const float axisAlignment = std::abs(tangent.dot(axis));
            if (axisAlignment < alignment)
            {
                alignment = axisAlignment;
                leastAligned = &axis;
            }
        }
        across = *leastAligned - tangent * leastAligned->dot(tangent);
    }
    frame.across.push_back(safeNormalized(across, Vector3(1, 0, 0)));

    // Carry the across direction along the curve, turning it by as much as the
    // tangent turns so the profile keeps its orientation.
    for (int pointIndex = 1; pointIndex < pointCount; ++pointIndex)
    {
        const Vector3& previousTangent = frame.tangents[pointIndex - 1];
        const Vector3& tangent = frame.tangents[pointIndex];
        const Vector3 axis = previousTangent.cross(tangent);
        const float sinAngle = axis.norm();

        Vector3 next = frame.across[pointIndex - 1];
        if (sinAngle > 1e-6f)
            next = rotateVector(next, axis / sinAngle,
                                std::atan2(sinAngle, previousTangent.dot(tangent)));
        else if (previousTangent.dot(tangent) < 0.0f)
            next = -next;

        // Keep the across direction square to the tangent.
        next = next - tangent * next.dot(tangent);
        frame.across.push_back(safeNormalized(next, frame.across[pointIndex - 1]));
    }

    return frame;
}

/**
 * @brief Builds a closed unit circle profile in the XY plane.
 *
 * The circle's normal runs along local Z so the frame can aim it down a curve
 * tangent. The points wind counter clockwise seen from that normal, the same
 * way the square profile winds, so the swept faces turn outward.
 */
std::vector<Vector3> buildCircleProfile(int columns)
{
    std::vector<Vector3> profile;
    profile.reserve(columns);
    for (int columnIndex = 0; columnIndex < columns; ++columnIndex)
    {
        const float angle = static_cast<float>(columnIndex) / columns * 2 * std::numbers::pi;
        profile.push_back({std::cos(angle), std::sin(angle), 0});
    }
    return profile;
}

/**
 * @brief Builds a closed unit square profile in the XY plane.
 *
 * Columns set how many segments each side is split into, so the corners always
 * land on points. One column gives a plain square, two adds the edge midpoints.
 */
std::vector<Vector3> buildSquareProfile(int columns)
{
    const Vector3 corners[] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};

    std::vector<Vector3> profile;
    profile.reserve(columns * 4);
    for (int corner = 0; corner < 4; ++corner)
    {
        // Walk from this corner toward the next, stopping short of it so the
        // next side contributes that shared corner.
        const Vector3& from = corners[corner];
        const Vector3& to = corners[(corner + 1) % 4];
        for (int step = 0; step < columns; ++step)
        {
            const float sideU = static_cast<float>(step) / columns;
            profile.push_back(from + (to - from) * sideU);
        }
    }
    return profile;
}

/**
 * @brief Builds a flat ribbon profile spanning the local X axis.
 *
 * The columns sit evenly from one edge to the other. The profile stays open so
 * the sweep lays a single flat strip rather than a doubled band.
 */
std::vector<Vector3> buildRibbonProfile(int columns)
{
    std::vector<Vector3> profile;
    profile.reserve(columns);
    for (int columnIndex = 0; columnIndex < columns; ++columnIndex)
    {
        const float widthU = static_cast<float>(columnIndex) / (columns - 1);
        profile.push_back({2 * widthU - 1, 0, 0});
    }
    return profile;
}

/**
 * @brief Builds a profile from the first curve of the second input.
 *
 * The profile is centred on its own middle, so the curve runs through the
 * middle of the shape rather than past one corner of it.
 *
 * @return The first mesh face's points and its open or closed state, empty when
 * the input carries no usable face.
 */
Profile buildProfileFromInput(NodePacket& packet)
{
    for (auto prim : packet.getPrimitives())
    {
        std::shared_ptr<geo::Mesh> mesh = std::dynamic_pointer_cast<geo::Mesh>(prim);
        if (!mesh || mesh->getNumFaces() == 0) continue;

        Profile profile;
        profile.closed = mesh->isClosed(0);

        Vector3 centre = Vector3(0, 0, 0);
        const std::span<const intT> facePoints = mesh->getFacePoints(0);
        for (intT pointOffset : facePoints) centre += mesh->getPointPos(pointOffset);
        if (!facePoints.empty()) centre /= static_cast<float>(facePoints.size());

        for (intT pointOffset : facePoints)
            profile.points.push_back(mesh->getPointPos(pointOffset) - centre);
        return profile;
    }
    return {};
}

/**
 * @brief A sweep of a profile along every curve of one mesh.
 *
 * Holds the mesh and the buffers the sweep fills so each curve is handled by
 * reading members rather than threading state through call after call.
 */
class Sweeper
{
  public:
    Sweeper(geo::Mesh& mesh, const Profile& profile, const SweepSettings& settings)
        : mesh_(mesh), profile_(profile),
          profilePointCount_(static_cast<int>(profile.points.size())), settings_(settings)
    {
    }

    /// @brief Replaces every curve with a tube surface swept from the profile.
    void sweep()
    {
        const std::vector<Offset> sourceCurves = mesh_.getFaces().toVector();

        // Sweep the profile along every curve, gathering all ring points for one
        // batched insert.
        ringPositions_.reserve(profilePointCount_ * mesh_.getNumPoints());
        for (Offset curveFace : sourceCurves)
            if (mesh_.getFacePointCount(curveFace) >= 2) addRings(curveFace);
        ringPoints_ = mesh_.addPoints(ringPositions_);

        // Bridge the rings into quad faces, walking the curves in the order their
        // rings were laid down so ringStart stays in step.
        size_t ringStart = 0;
        for (Offset curveFace : sourceCurves)
        {
            const int curvePointCount = mesh_.getFacePointCount(curveFace);
            if (curvePointCount < 2) continue;
            bridgeRings(curveFace, ringStart);
            if (settings_.capEnds) capEnds(curveFace, ringStart);
            ringStart += curvePointCount * profilePointCount_;
        }
        mesh_.addFaces(facePointOffsets_, faceVertexCounts_);

        // The cap faces go in last so they can be tagged into their own group.
        if (!capPointOffsets_.empty())
        {
            std::vector<Offset> capFaces = mesh_.addFaces(capPointOffsets_, capVertexCounts_);
            if (!settings_.capGroupName.empty())
            {
                mesh_.addFaceGroup(settings_.capGroupName);
                mesh_.addToFaceGroup(settings_.capGroupName, capFaces);
            }
        }

        // Remove the source curves now the swept surface replaces them.
        mesh_.deleteFaces(sourceCurves);
    }

  private:
    /// @brief Adds a ring of profile points at every point along the curve.
    void addRings(Offset curveFace)
    {
        const int curvePointCount = mesh_.getFacePointCount(curveFace);
        const bool closed = mesh_.isClosed(curveFace);
        const CurveFrame frame = buildCurveFrame(mesh_, curveFace);

        for (int relPointNum = 0; relPointNum < curvePointCount; ++relPointNum)
        {
            const Vector3 pointPos = mesh_.getPointPos(mesh_.getFacePoints(curveFace)[relPointNum]);

            // The ramp reads how far along the curve the point sits to set the
            // radius. With scaling off the profile keeps its full size. A closed
            // curve runs the whole ramp over one turn of the loop.
            const float curveU =
                settings_.applyScale
                    ? static_cast<float>(relPointNum) / (closed ? curvePointCount : curvePointCount - 1)
                    : 0.0f;
            const float radius = settings_.applyScale ? settings_.scaleRamp.sample(curveU) : 1.0f;

            // The across direction follows the curve, so it lines up across the
            // turn and only the width across it grows to keep the tube from
            // pinching there.
            const float stretchX =
                settings_.stretchTurns ? calculateStretch(frame, relPointNum) : 1.0f;

            // The scale runs first so it sizes the profile before the orientation
            // turns it.
            const Vector3 tangent = frame.tangents[relPointNum];
            const Vector3 binormal = tangent.cross(frame.across[relPointNum]);
            Transform transform = Transform::lookAt(pointPos, tangent, binormal);
            transform.scale(Vector3(radius * stretchX, radius, radius));

            for (const Vector3& profilePos : profile_.points)
                ringPositions_.push_back(transform * profilePos);
        }
    }

    /// @brief Returns how much to widen the profile across a turn so the tube keeps its width.
    float calculateStretch(const CurveFrame& frame, int relPointNum)
    {
        // The widening is capped so a near fold cannot run away.
        constexpr float maxStretch = 8.0f;

        // The profile faces along the average of the turn, so the sharper the
        // turn the more it leans away from each segment and the wider it grows.
        const float bendLean = frame.tangents[relPointNum].dot(frame.onward[relPointNum]);
        if (bendLean <= 1e-3f) return maxStretch;

        return std::min(1.0f / bendLean, maxStretch);
    }

    /// @brief Bridges the curve's neighbouring rings into quad faces.
    void bridgeRings(Offset curveFace, size_t ringStart)
    {
        const int curvePointCount = mesh_.getFacePointCount(curveFace);

        // A closed curve wraps its last ring back to its first, and a closed
        // profile wraps its last column back to its first.
        const int ringBridgeCount =
            mesh_.isClosed(curveFace) ? curvePointCount : curvePointCount - 1;
        const int columnBridgeCount = profile_.closed ? profilePointCount_ : profilePointCount_ - 1;
        for (int ring = 0; ring < ringBridgeCount; ++ring)
        {
            const size_t ringA = ringStart + ring * profilePointCount_;
            const size_t ringB = ringStart + ((ring + 1) % curvePointCount) * profilePointCount_;

            for (int column = 0; column < columnBridgeCount; ++column)
            {
                const int nextColumn = (column + 1) % profilePointCount_;

                facePointOffsets_.push_back(ringPoints_[ringA + column]);
                facePointOffsets_.push_back(ringPoints_[ringA + nextColumn]);
                facePointOffsets_.push_back(ringPoints_[ringB + nextColumn]);
                facePointOffsets_.push_back(ringPoints_[ringB + column]);
                faceVertexCounts_.push_back(4);
            }
        }
    }

    /// @brief Closes each open end of the curve with a single polygon.
    void capEnds(Offset curveFace, size_t ringStart)
    {
        // A closed curve has no open ends, and an open profile has no ring loop
        // to fill, so neither case gets a cap.
        if (mesh_.isClosed(curveFace) || !profile_.closed) return;

        const int curvePointCount = mesh_.getFacePointCount(curveFace);
        const size_t lastRing = ringStart + (curvePointCount - 1) * profilePointCount_;

        // The start cap winds opposite the end cap so both faces turn outward.
        for (int column = profilePointCount_ - 1; column >= 0; --column)
            capPointOffsets_.push_back(ringPoints_[ringStart + column]);
        capVertexCounts_.push_back(profilePointCount_);

        for (int column = 0; column < profilePointCount_; ++column)
            capPointOffsets_.push_back(ringPoints_[lastRing + column]);
        capVertexCounts_.push_back(profilePointCount_);
    }

    geo::Mesh& mesh_;
    const Profile& profile_;
    int profilePointCount_;
    const SweepSettings& settings_;

    std::vector<Vector3> ringPositions_;
    std::vector<Offset> ringPoints_;
    std::vector<Offset> facePointOffsets_;
    std::vector<Offset> faceVertexCounts_;
    std::vector<Offset> capPointOffsets_;
    std::vector<Offset> capVertexCounts_;
};

void sweepMesh(geo::Mesh& mesh, const Profile& profile, const SweepSettings& settings)
{
    Sweeper(mesh, profile, settings).sweep();
}

} // namespace

HELIX_REGISTER_NODE(sweep, Sweep)
