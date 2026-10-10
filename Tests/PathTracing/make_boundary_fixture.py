"""Create an editable finite-plane fixture with an exact constant primary background."""
import json
from validate_inputs import ROOT
from validate_part1 import write_json

BACKGROUND = [.125, .25, .5]


def main():
    source = ROOT/'Assets/Scenes/PathTracingValidation/input-plane'
    target = ROOT/'Assets/Scenes/PathTracingValidation/input-boundary'
    scene = json.loads((source/'scene.json').read_text())
    scene.update(sceneId='input-boundary', name='PT primary hit-miss boundary',
        description='Compare 1/2/4 samples per frame at finite-plane/background boundaries. Primary guides and hitT describe sample zero; signals average all surface samples; primary background contributes only to total radiance.')
    scene['nodes'][0]['primitive'].update(width=2, depth=2)
    preset = json.loads((source/'render-preset.json').read_text())
    preset['backBufferClearColor'] = BACKGROUND+[1]
    preset['lighting']['skyboxEnabled'] = False
    preset['pathTracing'].update(accumulate=False, maxBounces=2, environmentSamplingMode=1,
        samplesPerFrame=1, directLightingEnabled=False, emissiveEnabled=False)
    write_json(target/'scene.json', scene)
    write_json(target/'render-preset.json', preset)


if __name__ == '__main__':
    main()
