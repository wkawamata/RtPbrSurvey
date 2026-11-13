"""Generate the authored glass edge fixture; standard library only."""
import argparse
import base64
import copy
import json
import math
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def unit(v):
    length = math.sqrt(dot(v, v))
    return tuple(x/length for x in v)


def write_json(path, data):
    path.write_bytes((json.dumps(data, indent=2)+'\n').replace('\n', '\r\n').encode('utf-8'))


def orient(positions, normals, triangle):
    a, b, c = triangle
    n = cross(sub(positions[b], positions[a]), sub(positions[c], positions[a]))
    if dot(n, n) < 1e-20:
        return None
    normal = tuple(normals[a][i]+normals[b][i]+normals[c][i] for i in range(3))
    return triangle if dot(n, normal) > 0 else (a, c, b)


def rounded_box(size, radius, subdivisions):
    # Intersect tangent half-spaces instead of warping a quad grid: every face
    # stays planar and the triangulated solid satisfies the Volume convex guard.
    half = [x/2 for x in size]
    core = [h-radius for h in half]
    faces = []
    for axis in range(3):
        u, v = (axis+1)%3, (axis+2)%3
        for sign in (-1, 1):
            face = []
            for x, y in ((-1,-1), (1,-1), (1,1), (-1,1)):
                p = [0., 0., 0.]
                p[axis], p[u], p[v] = sign*half[axis], x*half[u], y*half[v]
                face.append(tuple(p))
            faces.append(face if sign > 0 else face[::-1])
    planes = []
    for axis in range(3):
        u, v = (axis+1)%3, (axis+2)%3
        for su in (-1, 1):
            for sv in (-1, 1):
                for i in range(1, subdivisions):
                    angle = math.pi*i/(2*subdivisions)
                    n = [0., 0., 0.]
                    n[u], n[v] = su*math.cos(angle), sv*math.sin(angle)
                    planes.append(tuple(n))
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                for i in range(1, subdivisions):
                    for j in range(1, subdivisions-i):
                        planes.append(unit((sx*i, sy*j, sz*(subdivisions-i-j))))
    for normal in planes:
        distance = sum(abs(n)*c for n, c in zip(normal, core))+radius
        clipped, boundary = [], []
        for face in faces:
            polygon = []
            for a, b in zip(face, face[1:]+face[:1]):
                da, db = dot(normal, a)-distance, dot(normal, b)-distance
                if da <= 1e-11:
                    polygon.append(a)
                if (da < -1e-11 and db > 1e-11) or (da > 1e-11 and db < -1e-11):
                    t = da/(da-db)
                    point = tuple(x+t*(y-x) for x, y in zip(a, b))
                    polygon.append(point)
                    boundary.append(point)
                elif abs(da) <= 1e-11:
                    boundary.append(a)
            if len(polygon) >= 3:
                clipped.append(polygon)
        unique = {tuple(round(x, 10) for x in p): p for p in boundary}
        if len(unique) >= 3:
            cap = list(unique.values())
            center = tuple(sum(p[i] for p in cap)/len(cap) for i in range(3))
            axis = (1, 0, 0) if abs(normal[0]) < .8 else (0, 1, 0)
            u = unit(cross(axis, normal)); v = cross(normal, u)
            cap.sort(key=lambda p: math.atan2(dot(sub(p, center), v), dot(sub(p, center), u)))
            clipped.append(cap)
        faces = clipped
    positions, normals, triangles = [], [], []
    for face in faces:
        start = len(positions)
        center = tuple(sum(p[i] for p in face)/len(face) for i in range(3))
        for point in face+[center]:
            positions.append(point)
            closest = [max(-c, min(c, p)) for p, c in zip(point, core)]
            normals.append(unit(sub(point, closest)))
        for i in range(len(face)):
            triangle = orient(positions, normals, (start+len(face), start+i, start+(i+1)%len(face)))
            if triangle:
                triangles.append(triangle)
    return positions, normals, triangles


def tumbler(wall, base, segments):
    # Analytic tangent continuity at the base fillets, tapered walls and rolled rim.
    profile = []
    def line(a, b):
        normal = unit((b[1]-a[1], a[0]-b[0]))
        profile.extend([(a, normal), (b, normal)])
    def bezier(a, b, c, d, steps=8):
        for i in range(steps+1):
            t = i/steps; q = 1-t
            point = tuple(q**3*a[k]+3*q*q*t*b[k]+3*q*t*t*c[k]+t**3*d[k] for k in range(2))
            tangent = tuple(3*q*q*(b[k]-a[k])+6*q*t*(c[k]-b[k])+3*t*t*(d[k]-c[k]) for k in range(2))
            profile.append((point, unit((tangent[1], -tangent[0]))))
    outer = .49
    lip = wall/2
    center_r, center_y = outer-lip, 1.65-lip
    slope = (outer-.405)/(center_y-.06)
    # A vertical tangent at the rolled rim is blended into the tapered body.
    line((0, 0), (.35, 0))
    bezier((.35, 0), (.385, 0), (.405-.02*slope, .04), (.405, .06))
    line((.405, .06), (outer-.07*slope, center_y-.07))
    bezier((outer-.07*slope, center_y-.07), (outer-.045*slope, center_y-.045),
           (outer, center_y-.025), (outer, center_y))
    for i in range(17):
        angle = math.pi*i/16
        profile.append(((center_r+lip*math.cos(angle), center_y+lip*math.sin(angle)),
                        (math.cos(angle), math.sin(angle))))
    inner = outer-wall
    bottom = .405-wall
    inner_slope = (inner-bottom)/(center_y-(base+.06))
    bezier((inner, center_y), (inner, center_y-.025),
           (inner-.045*inner_slope, center_y-.045), (inner-.07*inner_slope, center_y-.07))
    line((inner-.07*inner_slope, center_y-.07), (bottom, base+.06))
    bezier((bottom, base+.06), (bottom-.02*inner_slope, base+.04),
           (bottom-.025, base), (bottom-.055, base))
    line((bottom-.055, base), (0, base))
    # Remove duplicate joins; all joins have matching tangent normals.
    clean = []
    for p, n in profile:
        if clean and max(abs(x-y) for x, y in zip(p, clean[-1][0])) < 1e-10:
            continue
        clean.append((p, n))
    positions, normals, triangles = [], [], []
    for (r, y), (nr, ny) in clean:
        for i in range(segments):
            theta = 2*math.pi*i/segments
            positions.append((r*math.cos(theta), y, r*math.sin(theta)))
            normals.append((nr*math.cos(theta), ny, nr*math.sin(theta)))
    for j in range(len(clean)-1):
        for i in range(segments):
            a, b = j*segments+i, j*segments+(i+1)%segments
            for tri in ((a, b, a+segments), (b, b+segments, a+segments)):
                tri = orient(positions, normals, tri)
                if tri:
                    triangles.append(tri)
    return positions, normals, triangles


def save_mesh(path, mesh, material):
    positions, normals, triangles = mesh
    # Validate geometric manifold after welding seam/pole duplicates.
    keys = [tuple(round(x, 7) for x in p) for p in positions]
    edges = {}
    for tri in triangles:
        assert tri is not None
        for i in range(3):
            a, b = keys[tri[i]], keys[tri[(i+1)%3]]
            key = tuple(sorted((a, b)))
            edges.setdefault(key, []).append(1 if a < b else -1)
    assert all(len(signs) == 2 and sum(signs) == 0 for signs in edges.values()), path
    assert all(abs(dot(n, n)-1) < 1e-6 for n in normals)
    volume = sum(dot(positions[a], cross(positions[b], positions[c]))/6 for a,b,c in triangles)
    assert volume > 0
    blobs = [b''.join(struct.pack('<3f', *v) for v in values) for values in (positions, normals)]
    blobs.append(b''.join(struct.pack('<3I', *tri) for tri in triangles))
    data = b''.join(blobs)
    views = []; offset = 0
    for i, blob in enumerate(blobs):
        views.append(dict(buffer=0, byteOffset=offset, byteLength=len(blob), target=34963 if i == 2 else 34962))
        offset += len(blob)
    extensions = list(material['extensions'])
    doc = dict(asset=dict(version='2.0', generator='RtPbrSurvey generate_glass_edges.py'), scene=0,
               scenes=[dict(nodes=[0])], nodes=[dict(mesh=0, name=path.stem)],
               meshes=[dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=1), indices=2, material=0)])],
               buffers=[dict(byteLength=len(data), uri='data:application/octet-stream;base64,'+base64.b64encode(data).decode())],
               bufferViews=views,
               accessors=[dict(bufferView=0, componentType=5126, count=len(positions), type='VEC3',
                               min=[min(p[i] for p in positions) for i in range(3)],
                               max=[max(p[i] for p in positions) for i in range(3)]),
                          dict(bufferView=1, componentType=5126, count=len(normals), type='VEC3'),
                          dict(bufferView=2, componentType=5125, count=len(triangles)*3, type='SCALAR')],
               materials=[material], extensionsUsed=extensions)
    write_json(path, doc)
    return dict(vertices=len(positions), triangles=len(triangles), signedVolume=volume)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'Assets/Scenes/WarmGlassEdges')
    parser.add_argument('--segments', type=int, default=128)
    parser.add_argument('--bevel-subdivisions', type=int, default=3)
    args = parser.parse_args()
    assert args.segments >= 16 and args.bevel_subdivisions >= 1
    args.output.mkdir(parents=True, exist_ok=True)
    source = ROOT/'Assets/Scenes/WarmGlassThinShadows'
    scene = json.loads((source/'scene.json').read_text())
    scene.update(sceneId='warm-glass-edges', name='PT Warm Glass Edges',
                 description='Rounded rims and beveled glass panels. Same smooth thin optics and warm studio lighting as the thin-shadow control.')
    meshes = {}
    for name, wall, base in [('thin', .036, .11), ('thick', .132, .28)]:
        material = json.loads((source/f'{name}-tumbler.gltf').read_text())['materials'][0]
        meshes[f'{name}-tumbler'] = save_mesh(args.output/f'{name}-tumbler.gltf', tumbler(wall, base, args.segments), material)
    material = dict(name='Smooth thin cyan sheet', doubleSided=True,
                    pbrMetallicRoughness=dict(baseColorFactor=[.45, .85, 1, 1], metallicFactor=0, roughnessFactor=0),
                    extensions=dict(KHR_materials_transmission=dict(transmissionFactor=1), KHR_materials_ior=dict(ior=1.5)))
    meshes['thin-sheet'] = save_mesh(args.output/'thin-sheet.gltf', rounded_box((1.02, 1.82, .026), .004, args.bevel_subdivisions), material)
    material = copy.deepcopy(material)
    material.update(name='Smooth solid slab')
    material['pbrMetallicRoughness']['baseColorFactor'] = [1, 1, 1, 1]
    material['extensions']['KHR_materials_volume'] = dict(thicknessFactor=1)
    slab = rounded_box((1.03, 1.82, .30), .025, args.bevel_subdivisions)
    slab_node = next(n for n in scene['nodes'] if n['id'] == 'thick-slab')
    # Bake the original placement into the authored RH mesh. Re-transforming
    # tiny bevel faces in float at runtime can perturb their supporting planes.
    qy, qw = slab_node['rotation'][1], slab_node['rotation'][3]
    cosine, sine = 1-2*qy*qy, 2*qy*qw
    tx, ty, tz = slab_node['translation']
    def rotate(p):
        x, y, z = p
        return (cosine*x-sine*z, y, sine*x+cosine*z)
    positions = [tuple(x+t for x, t in zip(rotate(p), (tx, ty, -tz))) for p in slab[0]]
    normals = [rotate(n) for n in slab[1]]
    slab_node.update(translation=[0, 0, 0], rotation=[0, 0, 0, 1])
    meshes['thick-slab'] = save_mesh(args.output/'thick-slab.gltf',
                                    (positions, normals, slab[2]), material)
    for asset in scene['assets']:
        if asset['id'] not in ('thin-tumbler', 'thick-tumbler'):
            asset['path'] = Path(os.path.relpath(ROOT/'Assets/Scenes/WarmGlassTable'/Path(asset['path']).name, args.output)).as_posix()
    for name in ('thin-sheet', 'thick-slab'):
        scene['assets'].append(dict(id=name, type='gltf', path=name+'.gltf'))
    for entity in scene['nodes']:
        if entity['id'] in ('thin-sheet', 'thick-slab'):
            entity.update(type='gltf', assetId=entity['id'], scale=[1, 1, 1])
            entity.pop('primitive'); entity.pop('materialId')
    write_json(args.output/'scene.json', scene)
    preset = json.loads((source/'render-preset.json').read_text())
    write_json(args.output/'render-preset.json', preset)
    print(json.dumps(meshes, indent=2))


if __name__ == '__main__':
    main()
