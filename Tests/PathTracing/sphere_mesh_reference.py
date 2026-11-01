"""Double-precision triangle intersections over exported renderer geometry."""
import numpy as np


def prepare_triangles(positions, indices):
    vertices = np.asarray(positions, dtype=float)
    triangles = np.asarray(indices, dtype=int).reshape(-1, 3)
    if vertices.ndim != 2 or vertices.shape[1] != 3 or not np.isfinite(vertices).all():
        raise ValueError("Invalid mesh positions")
    if np.any(triangles < 0) or np.any(triangles >= len(vertices)):
        raise ValueError("Invalid mesh indices")
    v0, v1, v2 = (vertices[triangles[:, i]] for i in range(3))
    e1, e2 = v1-v0, v2-v0
    normals = np.cross(e1, e2)
    lengths = np.linalg.norm(normals, axis=1)
    valid = lengths > 1e-14
    return v0[valid], e1[valid], e2[valid], normals[valid]/lengths[valid, None]


def intersect_triangles(origin, direction, previous, inside, geometry):
    v0, e1, e2, normals = geometry
    p = np.cross(direction, e2)
    determinant = np.einsum('ij,ij->i', e1, p)
    inverse = np.divide(1., determinant, out=np.zeros_like(determinant), where=np.abs(determinant) > 1e-14)
    offset = origin-v0
    u = np.einsum('ij,ij->i', offset, p)*inverse
    q = np.cross(offset, e1)
    v = np.einsum('ij,j->i', q, direction)*inverse
    t = np.einsum('ij,ij->i', e2, q)*inverse
    valid = (np.abs(determinant) > 1e-14) & (u >= -1e-9) & (v >= -1e-9) & (u+v <= 1+1e-9) & (t >= 0)
    if previous >= 0:
        valid[previous] = False
    if inside:
        valid &= np.einsum('ij,j->i', normals, direction) > 0
    distances = np.where(valid, t, np.inf)
    face = int(np.argmin(distances))
    if not np.isfinite(distances[face]):
        raise ValueError("CPU triangle reference missed the solid")
    return float(distances[face]), face
