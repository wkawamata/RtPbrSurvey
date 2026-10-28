#pragma once

#include "Scene/VolumeMeshTopology.h"

#include <algorithm>
#include <optional>

namespace RtPbrSurvey
{
enum class VolumePointLocation
{
    Invalid,
    Outside,
    Boundary,
    Inside,
};

class ConvexVolumeBounds
{
public:
    using Position = std::array<float, 3>;

    static std::optional<ConvexVolumeBounds> Build(std::span<const Position> positions,
        std::span<const uint32_t> indices, std::string* error = nullptr)
    {
        const auto fail = [error](const char* message) -> std::optional<ConvexVolumeBounds>
        {
            if (error != nullptr)
            {
                *error = message;
            }
            return std::nullopt;
        };
        const std::string topologyError = ValidateVolumeMeshTopology(positions, indices);
        if (!topologyError.empty())
        {
            return fail(topologyError.c_str());
        }
        std::map<Position, bool> unique;
        for (uint32_t index : indices)
        {
            unique.emplace(positions[index], true);
        }
        constexpr size_t maxPlaneTests = 16 * 1024 * 1024;
        if (indices.size() / 3 > maxPlaneTests / unique.size())
        {
            return fail("Convex volume exceeds the plane-test validation budget.");
        }
        ConvexVolumeBounds result;
        result.minimum = unique.begin()->first;
        result.maximum = result.minimum;
        std::array<double, 3> center{};
        const Position origin = result.minimum;
        for (const auto& entry : unique)
        {
            for (size_t axis = 0; axis < 3; ++axis)
            {
                result.minimum[axis] = std::min(result.minimum[axis], entry.first[axis]);
                result.maximum[axis] = std::max(result.maximum[axis], entry.first[axis]);
                center[axis] += (static_cast<double>(entry.first[axis]) - origin[axis]) / unique.size();
            }
        }
        double extent = 0.0;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            extent = std::max(extent, static_cast<double>(result.maximum[axis]) - result.minimum[axis]);
            center[axis] += origin[axis];
        }
        result.tolerance = extent * 0.000001;
        for (size_t triangle = 0; triangle < indices.size(); triangle += 3)
        {
            Plane plane;
            plane.origin = positions[indices[triangle]];
            std::array<double, 3> a, b;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                a[axis] = static_cast<double>(positions[indices[triangle + 1]][axis]) - plane.origin[axis];
                b[axis] = static_cast<double>(positions[indices[triangle + 2]][axis]) - plane.origin[axis];
            }
            double lengthSquared = 0.0;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                plane.normal[axis] = a[(axis + 1) % 3] * b[(axis + 2) % 3] - a[(axis + 2) % 3] * b[(axis + 1) % 3];
                lengthSquared += plane.normal[axis] * plane.normal[axis];
            }
            const double length = std::sqrt(lengthSquared);
            for (double& value : plane.normal)
            {
                value /= length;
            }
            for (const auto& entry : unique)
            {
                if (plane.Distance(entry.first) > result.tolerance)
                {
                    return fail("Volume must be convex within its scale-relative tolerance.");
                }
            }
            if (plane.Distance(center) >= -result.tolerance)
            {
                return fail("Volume is too thin for reliable inside/outside classification.");
            }
            result.planes.push_back(plane);
        }
        if (error != nullptr)
        {
            error->clear();
        }
        return result;
    }

    VolumePointLocation ClassifyPoint(const Position& point) const
    {
        for (float value : point)
        {
            if (!std::isfinite(value))
            {
                return VolumePointLocation::Invalid;
            }
        }
        bool boundary = false;
        for (const Plane& plane : planes)
        {
            const double distance = plane.Distance(point);
            if (distance > tolerance)
            {
                return VolumePointLocation::Outside;
            }
            boundary = boundary || distance >= -tolerance;
        }
        return boundary ? VolumePointLocation::Boundary : VolumePointLocation::Inside;
    }

    // False proves AABB separation only. Invalid bounds and touching/overlapping boxes fail closed.
    bool MayOverlapBounds(const Position& otherMinimum, const Position& otherMaximum) const
    {
        for (size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(otherMinimum[axis]) || !std::isfinite(otherMaximum[axis]) ||
                otherMinimum[axis] > otherMaximum[axis])
            {
                return true;
            }
        }
        for (size_t axis = 0; axis < 3; ++axis)
        {
            if (static_cast<double>(otherMaximum[axis]) < static_cast<double>(minimum[axis]) - tolerance ||
                static_cast<double>(otherMinimum[axis]) > static_cast<double>(maximum[axis]) + tolerance)
            {
                return false;
            }
        }
        return true;
    }

private:
    struct Plane
    {
        Position origin{};
        std::array<double, 3> normal{};

        template <typename T>
        double Distance(const std::array<T, 3>& point) const
        {
            double value = 0.0;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                value += (static_cast<double>(point[axis]) - origin[axis]) * normal[axis];
            }
            return value;
        }
    };

    ConvexVolumeBounds() = default;
    std::vector<Plane> planes;
    Position minimum{};
    Position maximum{};
    double tolerance = 0.0;
};
}
