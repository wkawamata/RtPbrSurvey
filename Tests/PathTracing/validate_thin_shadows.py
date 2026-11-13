"""Analytic RGB direct-shadow tests on a receiver viewed clear of the occluder.

One-sample, one-bounce Native captures isolate delta analytic lighting. This is
not a rough/volume/caustic or environment/emissive MIS qualification.
"""
import argparse
import base64
import copy
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from measure_rr_release import ROOT, require_isolation
from validate_inputs import read_buffer


def fresnel(cosine, ior):
    if ior == 1:
        return 0.0
    transmitted = np.sqrt(1-(1-cosine*cosine)/(ior*ior))
    return .5*(((ior*cosine-transmitted)/(ior*cosine+transmitted))**2+
               ((cosine-ior*transmitted)/(cosine+ior*transmitted))**2)


def optical_plane(folder, alpha=False):
    vertices=np.array([[-.4,-1,0],[-.4,1,0],[.4,1,0],[.4,-1,0]],dtype='<f4')
    normals=np.array([[0,0,1]]*4,dtype='<f4')
    uvs=np.array([[0,0],[0,1],[1,1],[1,0]],dtype='<f4')
    indices=np.array([0,1,2,0,2,3],dtype='<u2')
    pieces=[vertices.tobytes(),normals.tobytes(),uvs.tobytes(),indices.tobytes()]
    offsets=[0,len(pieces[0]),len(pieces[0])+len(pieces[1]),sum(map(len,pieces[:3]))]
    views=[dict(buffer=0,byteOffset=o,byteLength=len(p)) for o,p in zip(offsets,pieces)]
    accessors=[dict(bufferView=i,componentType=5126,count=4,type=t) for i,t in enumerate(('VEC3','VEC3','VEC2'))]
    accessors[0].update(min=vertices.min(axis=0).tolist(),max=vertices.max(axis=0).tolist())
    accessors.append(dict(bufferView=3,componentType=5123,count=6,type='SCALAR'))
    Image.new('RGBA',(1,1),(255,255,255,0) if alpha else (128,64,255,255)).save(folder/'optics.png')
    material=dict(doubleSided=True,pbrMetallicRoughness=dict(baseColorFactor=[1,1,1,1],
                  baseColorTexture=dict(index=0),metallicFactor=0,roughnessFactor=0))
    if alpha:
        material.update(alphaMode='MASK',alphaCutoff=.5)
    else:
        material['extensions']={'KHR_materials_transmission':dict(transmissionFactor=1,transmissionTexture=dict(index=0)),
                                'KHR_materials_ior':dict(ior=1.5)}
    binary=b''.join(pieces)
    gltf=dict(asset=dict(version='2.0'),scene=0,scenes=[dict(nodes=[0])],nodes=[dict(mesh=0)],
              meshes=[dict(primitives=[dict(attributes=dict(POSITION=0,NORMAL=1,TEXCOORD_0=2),indices=3,material=0)])],
              materials=[material],images=[dict(uri='optics.png')],textures=[dict(source=0)],
              buffers=[dict(byteLength=len(binary),uri='data:application/octet-stream;base64,'+base64.b64encode(binary).decode())],
              bufferViews=views,accessors=accessors)
    if not alpha:
        gltf['extensionsUsed']=['KHR_materials_transmission','KHR_materials_ior']
    (folder/'plane.gltf').write_text(json.dumps(gltf),encoding='utf-8')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    exe,output=args.exe.resolve(strict=True),args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Output must be empty')
    require_isolation()
    output.mkdir(parents=True,exist_ok=True)
    base_scene=json.loads((ROOT/'Assets/Scenes/WarmGlassTable/scene.json').read_text(encoding='utf-8'))
    base_preset=json.loads((ROOT/'Assets/Scenes/WarmGlassTable/render-preset.json').read_text(encoding='utf-8'))
    base_scene.update(assets=[],nodes=[],materials=[],renderPreset='preset.json',sceneId='thin-shadow-check')
    base_scene['camera'].update(position=[0,0,-5],target=[0,0,0],verticalFovDegrees=20)
    base_scene['environment']['iblEnabled']=False
    base_preset['lighting'].update(lights=[dict(id=1,name='Analytic key',type='directional',enabled=True,
        color=[1,1,1],intensity=2,position=[2,0,-3],direction=[-2,0,3],range=100,innerCone=15,outerCone=30)],
        primaryShadowLightId=1,directLightEnabled=True,diffuseIblEnabled=False,specularIblEnabled=False,
        skyboxEnabled=False,iblIntensity=0,emissiveEnabled=False)
    base_preset['pathTracing'].update(accumulate=False,samplesPerFrame=1,maxBounces=1,randomSeed=7,
        directLightingEnabled=True,environmentEnabled=False,emissiveEnabled=False,pixelCenterPrimary=True,
        denoiserDistanceDiagnostics=False,nrdPreview=False,reconstructionBackend='native',debugOutput=3)
    base_preset['shadow'].update(enabled=True,normalBias=.0001,rayTMin=.0001)
    rotation=[-2**-.5,0,0,2**-.5]
    def node(name,z,material='glass',box=False,flip=False):
        return dict(id=name,name=name,parentId=None,type='primitive',materialId=material,visible=True,
                    primitive=dict(kind='cube',size=1) if box else dict(kind='plane',width=.8,depth=2),
                    translation=[-z*2/3,0,z],scale=[1,2,.1] if box else [1,1,1],
                    rotation=[0,0,0,1] if box else ([2**-.5,0,0,2**-.5] if flip else rotation))
    receiver=dict(id='receiver',name='Receiver',parentId=None,type='primitive',materialId='receiver',visible=True,
                  primitive=dict(kind='plane',width=6,depth=6),translation=[0,0,0],scale=[1,1,1],rotation=rotation)
    report=dict(status='running',executableSha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                captures=[],checks={},failures=[],roi=[48,48,81,81])
    def save():
        (output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    def capture(name,nodes=(),tint=(1,1,1),ior=1,factor=1,metallic=0,roughness=0,debug=False,asset=None,point=False):
        require_isolation()
        folder=output/name
        folder.mkdir()
        scene,preset=copy.deepcopy(base_scene),copy.deepcopy(base_preset)
        scene['nodes']=[receiver]+copy.deepcopy(list(nodes))
        scene['materials']=[dict(id='receiver',name='Receiver',baseColor=[.5,.5,.5,1],metallic=0,roughness=1),
            dict(id='opaque',name='Opaque',baseColor=[.5,.5,.5,1],metallic=0,roughness=1)]
        if nodes:
            scene['materials'].append(dict(id='glass',name='Glass',baseColor=[1,1,1,1],metallic=metallic,roughness=roughness,
                transmission=dict(mode='thin',factor=factor,tint=list(tint),ior=ior)))
        if asset:
            optical_plane(folder,alpha=asset=='alpha')
            scene['assets']=[dict(id='plane',type='gltf',path='plane.gltf')]
            scene['nodes'][-1]=dict(id='asset',name='Asset',parentId=None,type='gltf',assetId='plane',visible=True,
                                    translation=[1,0,-1.5],rotation=[0,0,0,1],scale=[1,1,1])
        if debug:
            preset['pathTracing']['debugOutput']=9
        if point:
            preset['lighting']['lights'][0].update(type='point',intensity=26)
        for fn,data in (('scene.json',scene),('preset.json',preset)):
            (folder/fn).write_text(json.dumps(data,indent=2),encoding='utf-8')
        command=[str(exe),'-SceneFile',str(folder/'scene.json'),'-PathTracingReconstruction','native',
            '-WindowSize','129','129','-DebugPreviewResource','PathTracing.SceneColor','-CaptureAfterFrames','4',
            '-CapturePath',str(folder/'color.ptbuf'),'-LogToFile',str(folder/'runtime.log'),'-ExitAfterCapture']
        print('Capturing '+name,flush=True)
        startup=subprocess.STARTUPINFO();startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=subprocess.SW_HIDE
        child=subprocess.Popen(command,cwd=ROOT,startupinfo=startup)
        try:
            code=child.wait(timeout=90)
        finally:
            if child.poll() is None:
                child.kill();child.wait()
        require_isolation()
        text=(folder/'runtime.log').read_text(encoding='utf-8-sig')
        if code or any(l.startswith(('[ERROR]','[CORRUPTION]')) for l in text.splitlines()):
            raise ValueError('Capture failed: '+name)
        meta,image=read_buffer(folder/'color.ptbuf')
        if (meta['width'],meta['height'])!=(129,129):
            raise ValueError('Unexpected dimensions')
        report['captures'].append(dict(name=name,command=command))
        save()
        return image[48:81,48:81,:3]
    def compare(name,actual,expected):
        expected=np.broadcast_to(expected,actual.shape)
        difference=np.abs(actual-expected)
        tolerance=1e-5+.003*np.abs(expected)
        if not np.all(difference<=tolerance):
            raise ValueError(f'{name}: RGB max error {difference.max()}')
        report['checks'][name]=dict(maxAbsError=float(difference.max()),pixels=actual.shape[0]*actual.shape[1])
    save()
    try:
        baseline=capture('baseline')
        if float(baseline.min())<=.001:
            raise ValueError('Receiver must be lit and visible')
        pane=[node('pane',-1.5)]
        compare('ior-one',capture('ior-one',pane),baseline)
        tint=np.array([.2,.5,.8])
        compare('colored',capture('colored',pane,tint=tint),baseline*tint)
        cosine=3/np.sqrt(13)
        ft=1-fresnel(cosine,1.5)
        compare('fresnel',capture('fresnel',pane,tint=tint,ior=1.5),baseline*tint*ft)
        compare('mixture',capture('mixture',pane,tint=tint,ior=1.5,factor=.4,metallic=.25),baseline*tint*ft*.4*.75)
        compare('backface',capture('backface',[node('pane',-1.5,flip=True)],tint=tint,ior=1.5),baseline*tint*ft)
        compare('two-sheets',capture('two-sheets',[node('a',-1),node('b',-2)],tint=tint,ior=1.5),baseline*(tint*ft)**2)
        compare('closed-two-interfaces',capture('closed-two-interfaces',[node('box',-1.5,box=True)],tint=tint,ior=1.5),baseline*(tint*ft)**2)
        compare('opaque',capture('opaque',[node('block',-1.5,material='opaque')]),np.zeros(3))
        compare('opaque-after-glass',capture('opaque-after-glass',pane+[node('block',-2,material='opaque')]),np.zeros(3))
        compare('factor-zero',capture('factor-zero',pane,factor=0),np.zeros(3))
        compare('metallic-one',capture('metallic-one',pane,metallic=1),np.zeros(3))
        compare('rough-conservative',capture('rough-conservative',pane,ior=1.5,roughness=.1),np.zeros(3))
        compare('rough-status',capture('rough-status',pane,ior=1.5,roughness=.1,debug=True),np.array([2,0,1]))
        compare('alpha-cutout',capture('alpha-cutout',pane,asset='alpha'),baseline)
        tex=np.array([128,64,255])/255
        linear=np.where(tex<=.04045,tex/12.92,((tex+.055)/1.055)**2.4)
        compare('textures',capture('textures',pane,asset='optics'),baseline*linear*(128/255)*ft)
        sheets=[node(f'pane-{i}',-1-i*.1) for i in range(17)]
        compare('budget-exact',capture('budget-exact',sheets[:16]),baseline)
        compare('budget-exact-status',capture('budget-exact-status',sheets[:16],debug=True),np.array([0,16,17]))
        compare('budget-overflow',capture('budget-overflow',sheets),np.zeros(3))
        compare('budget-overflow-status',capture('budget-overflow-status',sheets,debug=True),np.array([1,16,17]))
        point_base=capture('point-baseline',point=True)
        compare('point',capture('point',pane,tint=tint,point=True),point_base*tint)
        compare('behind-light',capture('behind-light',pane+[node('far-block',-4,material='opaque')],point=True),point_base)
        report['status']='passed'
    except Exception as error:
        report['status']='failed';report['failures'].append(str(error));raise
    finally:
        save()


if __name__=='__main__':
    main()
