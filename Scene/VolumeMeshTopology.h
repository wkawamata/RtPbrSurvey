#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace RtPbrSurvey
{
// Topology only: self-intersections and placement relative to other media are not tested.
inline std::string ValidateVolumeMeshTopology(std::span<const std::array<float, 3>> positions,
    std::span<const uint32_t> indices)
{
    if (positions.empty() || indices.empty() || indices.size() % 3 != 0)
    {
        return "Volume requires an indexed triangle mesh.";
    }
    std::map<std::array<float, 3>, size_t> welded;
    std::vector<size_t> vertexIds;
    for (const auto& position : positions)
    {
        for (float value : position)
        {
            if (!std::isfinite(value))
            {
                return "Volume vertex positions must be finite.";
            }
        }
        const auto entry = welded.emplace(position, welded.size());
        vertexIds.push_back(entry.first->second);
    }
    struct Edge
    {
        std::vector<size_t> faces;
        int direction = 0;
    };
    std::map<std::array<size_t, 2>, Edge> edges;
    const size_t faceCount = indices.size() / 3;
    std::vector<std::vector<size_t>> incident(welded.size());
    double signedVolume = 0.0;
    const auto& origin = positions.front();
    for (size_t face = 0; face < faceCount; ++face)
    {
        std::array<size_t, 3> ids;
        std::array<std::array<double, 3>, 3> points;
        for (size_t corner = 0; corner < 3; ++corner)
        {
            const uint32_t index = indices[face * 3 + corner];
            if (index >= positions.size())
            {
                return "Volume index is out of range.";
            }
            ids[corner] = vertexIds[index];
            for (size_t axis = 0; axis < 3; ++axis)
            {
                points[corner][axis] = static_cast<double>(positions[index][axis]) - origin[axis];
            }
        }
        std::array<double, 3> a, b, cross;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            a[axis] = points[1][axis] - points[0][axis];
            b[axis] = points[2][axis] - points[0][axis];
        }
        for (size_t axis = 0; axis < 3; ++axis)
        {
            cross[axis] = a[(axis + 1) % 3] * b[(axis + 2) % 3] - a[(axis + 2) % 3] * b[(axis + 1) % 3];
        }
        if (cross[0] == 0.0 && cross[1] == 0.0 && cross[2] == 0.0)
        {
            return "Volume contains a degenerate triangle.";
        }
        for (size_t axis = 0; axis < 3; ++axis)
        {
            signedVolume += points[0][axis] * cross[axis] / 6.0;
            incident[ids[axis]].push_back(face);
            const size_t from = ids[axis];
            const size_t to = ids[(axis + 1) % 3];
            const std::array<size_t, 2> key = from < to ? std::array<size_t, 2>{from, to} : std::array<size_t, 2>{to, from};
            Edge& edge = edges[key];
            edge.faces.push_back(face);
            edge.direction += from < to ? 1 : -1;
        }
    }
    std::vector<std::vector<size_t>> neighbors(faceCount);
    std::vector<std::vector<std::array<size_t, 2>>> vertexLinks(welded.size());
    for (const auto& [vertices, edge] : edges)
    {
        if (edge.faces.size() != 2 || edge.direction != 0)
        {
            return "Volume edges must have exactly two oppositely oriented faces.";
        }
        neighbors[edge.faces[0]].push_back(edge.faces[1]);
        neighbors[edge.faces[1]].push_back(edge.faces[0]);
        for (size_t vertex : vertices)
        {
            vertexLinks[vertex].push_back({edge.faces[0], edge.faces[1]});
        }
    }
    const auto connected = [](const std::vector<std::vector<size_t>>& adjacency, size_t start)
    {
        std::vector<bool> visited(adjacency.size(), false);
        std::vector<size_t> pending{start};
        visited[start] = true;
        size_t count = 0;
        while (!pending.empty())
        {
            const size_t face = pending.back();
            pending.pop_back();
            ++count;
            for (size_t next : adjacency[face])
            {
                if (!visited[next])
                {
                    visited[next] = true;
                    pending.push_back(next);
                }
            }
        }
        return count;
    };
    if (connected(neighbors, 0) != faceCount)
    {
        return "Volume must contain one connected shell.";
    }
    for (size_t vertex = 0; vertex < incident.size(); ++vertex)
    {
        if (incident[vertex].empty())
        {
            continue;
        }
        std::map<size_t, size_t> localIds;
        for (size_t face : incident[vertex])
        {
            localIds.emplace(face, localIds.size());
        }
        std::vector<std::vector<size_t>> fan(localIds.size());
        for (const auto& link : vertexLinks[vertex])
        {
            const size_t a = localIds.at(link[0]);
            const size_t b = localIds.at(link[1]);
            fan[a].push_back(b);
            fan[b].push_back(a);
        }
        if (connected(fan, 0) != fan.size())
        {
            return "Volume contains a non-manifold vertex fan.";
        }
    }
    if (!std::isfinite(signedVolume) || signedVolume <= 0.0)
    {
        return "Volume shell must have positive signed volume and outward winding.";
    }
    return {};
}
}
