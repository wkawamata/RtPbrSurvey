"""Reproduce tiny self-contained glTF assets for native PT material-guide tests."""
import base64
import copy
import json
import struct
import zlib
from validate_inputs import ROOT
from validate_part1 import write_json

COLORS = [(64, 128, 192, 255), (192, 64, 32, 255), (32, 192, 64, 255), (128, 32, 192, 255)]
NORMALS = [(192, 128, 240, 255), (64, 128, 240, 255), (128, 192, 240, 255), (128, 64, 240, 255)]
ROUGHNESS = [64, 128, 192, 224]
BASE_FACTOR = [.8, .6, .4, 1]
ROUGHNESS_FACTOR = .7


def quadrant_png(colors):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data) & 0xffffffff)
    pixels = b''.join(b'\0' + b''.join(bytes(colors[(y >= 8)*2+(x >= 8)]) for x in range(16)) for y in range(16))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 16, 16, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


def make_gltf(normal_scale):
    positions = [(-10, -10, 0), (10, -10, 0), (10, 10, 0), (-10, 10, 0)]
    arrays = [(positions, '3f', 'VEC3'), ([(0, 0, 1)]*4, '3f', 'VEC3'),
              ([(0, 0), (1, 0), (1, 1), (0, 1)], '2f', 'VEC2'),
              ([(1, 0, 0, 1)]*4, '4f', 'VEC4'), ([(0,), (1,), (2,), (0,), (2,), (3,)], 'H', 'SCALAR')]
    binary = bytearray()
    views, accessors = [], []
    for values, fmt, kind in arrays:
        binary.extend(b'\0' * (-len(binary) % 4))
        offset = len(binary)
        packed = b''.join(struct.pack('<'+fmt, *value) for value in values)
        binary.extend(packed)
        views.append(dict(buffer=0, byteOffset=offset, byteLength=len(packed)))
        accessor = dict(bufferView=len(views)-1, componentType=5123 if fmt == 'H' else 5126,
                        count=len(values), type=kind)
        if len(accessors) == 0:
            accessor.update(min=[-10, -10, 0], max=[10, 10, 0])
        accessors.append(accessor)
    images = [quadrant_png(COLORS), quadrant_png(NORMALS),
              quadrant_png([(255, rough, 0, 255) for rough in ROUGHNESS])]
    return dict(asset=dict(version='2.0', generator='RtPbrSurvey deterministic PT fixture'), scene=0,
        scenes=[dict(nodes=[0])], nodes=[dict(mesh=0)],
        meshes=[dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=1, TEXCOORD_0=2, TANGENT=3),
                                     indices=4, material=0)])],
        buffers=[dict(byteLength=len(binary), uri='data:application/octet-stream;base64,'+base64.b64encode(binary).decode())],
        bufferViews=views, accessors=accessors,
        images=[dict(uri='data:image/png;base64,'+base64.b64encode(image).decode()) for image in images],
        samplers=[dict(magFilter=9729, minFilter=9729, wrapS=33071, wrapT=33071)],
        textures=[dict(source=i, sampler=0) for i in range(3)],
        materials=[dict(pbrMetallicRoughness=dict(baseColorFactor=BASE_FACTOR, baseColorTexture=dict(index=0),
            metallicFactor=0, roughnessFactor=ROUGHNESS_FACTOR, metallicRoughnessTexture=dict(index=2)),
            normalTexture=dict(index=1, scale=normal_scale), doubleSided=True)])


def main():
    source = ROOT/'Assets/Scenes/PathTracingValidation/input-plane'
    target = ROOT/'Assets/Scenes/PathTracingValidation/input-textured'
    scene = json.loads((source/'scene.json').read_text(encoding='utf-8'))
    preset = json.loads((source/'render-preset.json').read_text(encoding='utf-8'))
    write_json(target/'render-preset.json', preset)
    for name, scale, mirror in [('mapped', .5, False), ('flat', 0, False), ('mirrored', .5, True)]:
        model = make_gltf(scale)
        if mirror:
            model['nodes'][0]['scale'] = [-1, 1, 1]
        write_json(target/(name+'.gltf'), model)
        case = copy.deepcopy(scene)
        case.update(sceneId='input-textured-'+name, name='PT textured guides '+name, materials=[],
            assets=[dict(id='plane', type='gltf', path=name+'.gltf')],
            nodes=[dict(id='receiver', name='Textured receiver', parentId=None, type='gltf',
                translation=[0, 0, 0], rotation=[0, 0, 0, 1], scale=[1, 1, 1],
                visible=True, assetId='plane')],
            description='Validate PT primary Albedo and NormalRoughness: four texture regions, linear color, roughness data, normal scale and mirrored tangent frame.')
        write_json(target/('scene.json' if name == 'mapped' else 'scene-'+name+'.json'), case)


if __name__ == '__main__':
    main()
