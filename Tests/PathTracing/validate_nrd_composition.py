"""Compare live material restoration/composition with an independent CPU expression."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_denoiser_distances import capture
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('NrdOutputDiffuse', 'NrdOutputSpecular', 'NrdDiffuseFactor', 'NrdSpecularFactor',
    'StableRadiance', 'BackgroundRadiance', 'SceneColor', 'NrdInputAdmission',
    'NrdSceneColor', 'NrdCompositionDiagnostics', 'PhysicalDiffuse', 'PhysicalSpecular',
    'NrdDiffuseCandidate', 'NrdSpecularCandidate', 'NrdValidity')


def expected_composition(diffuse, specular, df, sf, stable, background, native, mask):
    shape = native.shape
    if len(shape) < 2 or shape[-1] != 4 or native.size == 0 or any(
            value.shape != shape for value in (diffuse, specular, df, sf, stable, background, mask)):
        raise ValueError('Malformed composition cohort')
    values = [np.asarray(value, dtype=np.float32) for value in (diffuse, specular, df, sf, stable, background, native, mask)]
    diffuse, specular, df, sf, stable, background, native, mask = values
    native_valid = np.isfinite(native).all(axis=-1) & (native[..., :3] >= 0).all(axis=-1) & (native[..., 3] == 1)
    code = np.full(shape[:-1], 6, dtype=np.float32)
    code[native_valid] = 2
    mask_valid = (np.isfinite(mask).all(axis=-1) & np.isin(mask[..., [0, 2, 3]], [0, 1]).all(axis=-1) &
        np.isin(mask[..., 1], [0, 1, 2, 3, 4]) & ((mask[..., 0] != 1) | (mask[..., 1] == 0)) &
        ((mask[..., 0] != 0) | (mask[..., 2:] == 0).all(axis=-1)))
    eligible = native_valid & mask_valid
    code[eligible] = 1
    eligible &= mask[..., 0] == 1
    code[eligible] = 3
    for factor in (df, sf):
        eligible &= (np.isfinite(factor).all(axis=-1) & (factor[..., 3] == 1) &
            (factor[..., :3] > 0).all(axis=-1) & (factor[..., :3] <= 1).all(axis=-1))
    code[eligible] = 4
    for signal in (diffuse, specular, stable, background):
        eligible &= np.isfinite(signal[..., :3]).all(axis=-1) & (signal[..., :3] >= 0).all(axis=-1)
    code[eligible] = 5
    with np.errstate(invalid='ignore', over='ignore'):
        restored_diffuse = np.where(mask[..., 2:3] == 1, 0, diffuse[..., :3] * df[..., :3])
        restored_specular = np.where(mask[..., 3:4] == 1, 0, specular[..., :3] * sf[..., :3])
        rgb = ((restored_diffuse + restored_specular) + stable[..., :3]) + background[..., :3]
    used = eligible & np.isfinite(rgb).all(axis=-1) & (rgb >= 0).all(axis=-1)
    code[used] = 0
    color = np.zeros(shape, dtype=np.float32)
    color[..., 3] = 1
    color[native_valid] = native[native_valid]
    color[used, :3] = rgb[used]
    diagnostics = np.zeros(shape, dtype=np.float32)
    diagnostics[..., 0] = used
    diagnostics[..., 1] = code
    diagnostics[used, 2:] = mask[used, 2:]
    return color, diagnostics


def assess_composition(buffers):
    reference, diagnostics = expected_composition(*(buffers[name] for name in NAMES[:8]))
    actual = buffers['NrdSceneColor']
    used = diagnostics[..., 0] == 1
    if not np.array_equal(buffers['NrdCompositionDiagnostics'], diagnostics):
        raise ValueError('Composition diagnostic classification mismatch')
    if not np.isfinite(actual).all() or (actual < 0).any() or (actual[..., 3] != 1).any():
        raise ValueError('Invalid scene-color output')
    if not np.array_equal(actual[~used], buffers['SceneColor'][~used]):
        raise ValueError('Native fallback is not bit-exact')
    bound = 8*np.abs(np.spacing(np.abs(reference).astype(np.float32)).astype(np.float64)) + 1e-6
    error = abs(actual-reference)
    if not (error <= bound).all():
        raise ValueError('Remodulation/composition arithmetic mismatch')
    converted = buffers['NrdValidity'][..., 0] == 1
    roundtrip = {}
    for lobe in ('Diffuse', 'Specular'):
        factor = buffers['Nrd'+lobe+'Factor']
        if (not np.isfinite(factor).all() or (factor[converted, 3] != 1).any() or
                (factor[converted, :3] <= 0).any() or (factor[converted, :3] > 1).any() or (factor[~converted] != 0).any()):
            raise ValueError('Invalid stored factor validity or range')
        physical = buffers['Physical'+lobe][converted, :3]
        restored = buffers['Nrd'+lobe+'Candidate'][converted, :3] * factor[converted, :3]
        tolerance = 8*np.abs(np.spacing(np.abs(physical).astype(np.float32)).astype(float)) + 1e-6
        if not (abs(restored-physical) <= tolerance).all():
            raise ValueError('Stored factors do not invert the original conversion')
        roundtrip[lobe] = float(abs(restored-physical).max(initial=0))
    return dict(usedPixels=int(used.sum()), fallbackPixels=int((~used).sum()),
        inactiveDiffusePixels=int(((diagnostics[..., 2] == 1) & used).sum()),
        maximumAbsoluteError=float(error.max()), maximumBoundRatio=float((error/bound).max()),
        factorRoundtripMaximumError=roundtrip,
        nonzeroStablePixels=int((buffers['StableRadiance'][..., :3] > 0).any(axis=-1).sum()),
        nonzeroBackgroundPixels=int((buffers['BackgroundRadiance'][..., :3] > 0).any(axis=-1).sum()),
        arithmeticBound='8 ULP(reference float32) + 1e-6', fallbackComparator='exact decoded values (Native float16 to float32)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--disabled-distance-diagnostics', action='store_true')
    args = parser.parse_args()
    source, folder = args.source.resolve(strict=True), args.output.resolve()
    if source == folder or source in folder.parents or (folder.exists() and any(folder.iterdir())):
        parser.error('Output must be a separate empty directory')
    scene = json.loads((source/'scene.json').read_text())
    scene['materials'][0]['metallic'] = 1.0
    preset = json.loads((source/('preset.json' if (source/'preset.json').exists() else 'on.json')).read_text())
    preset['pathTracing'].update(accumulate=True, samplesPerFrame=1, pixelCenterPrimary=True,
        denoiserDistanceDiagnostics=not args.disabled_distance_diagnostics)
    write_json(folder/'scene.json', scene)
    write_json(folder/'preset.json', preset)
    report = dict(status='running', fullIntegrationReady=False, records=[], failures=[],
        executableSha256=sha(ROOT/'build/Debug/RtPbrSurvey.exe'), sceneSha256=sha(folder/'scene.json'),
        presetSha256=sha(folder/'preset.json'), scriptSha256=sha(Path(__file__)),
        compositionShaderSha256=sha(ROOT/'build/shaders/shaders_NrdComposition_CSMain.cso'),
        conversionShaderSha256=sha(ROOT/'build/shaders/shaders_NrdConversion_CSMain.cso'),
        helperSha256=sha(ROOT/'Shaders/NrdRelaxComposition.hlsli'))
    write_json(folder/'report.json', report)
    try:
        buffers, common = {}, None
        names = NAMES if not args.disabled_distance_diagnostics else (
            'SceneColor', 'NrdInputAdmission', 'NrdSceneColor', 'NrdCompositionDiagnostics', 'NrdDiffuseFactor', 'NrdSpecularFactor')
        for name in names:
            (meta, image), record = capture(ROOT/'build/Debug/RtPbrSurvey.exe', folder, name, folder/'preset.json', name)
            report['records'].append(record)
            if meta['resource'] != 'PathTracing.'+name or meta['format'] != (10 if name == 'SceneColor' else 2):
                raise ValueError('Wrong capture identity or format')
            comparable = {key: value for key, value in meta.items() if key not in ('resource', 'format')}
            if common is not None and comparable != common:
                raise ValueError('Mismatched composition capture cohort')
            common, buffers[name] = comparable, image
            write_json(folder/'report.json', report)
        if args.disabled_distance_diagnostics:
            native = buffers['SceneColor']
            zero = np.zeros_like(native)
            expected, diagnostics = expected_composition(zero, zero, buffers['NrdDiffuseFactor'],
                buffers['NrdSpecularFactor'], zero, zero, native, buffers['NrdInputAdmission'])
            if ((buffers['NrdDiffuseFactor'] != 0).any() or (buffers['NrdSpecularFactor'] != 0).any() or
                (buffers['NrdInputAdmission'][..., 0] != 0).any() or
                (diagnostics[..., 1] != 1).any() or not np.array_equal(expected, native) or
                not np.array_equal(buffers['NrdSceneColor'], native) or
                not np.array_equal(buffers['NrdCompositionDiagnostics'], diagnostics)):
                raise ValueError('Disabled inputs failed Native fallback or factor clear')
            assessment = dict(usedPixels=0, fallbackPixels=int(np.prod(native.shape[:2])),
                fallbackComparator='exact decoded values (Native float16 to float32)')
        else:
            assessment = assess_composition(buffers)
            if min(assessment['usedPixels'], assessment['fallbackPixels'], assessment['inactiveDiffusePixels']) < 100:
                raise ValueError('Fixture lacks required composition/fallback/inactive coverage')
        report.update(status='passed', assessment=assessment)
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = 'Independent diagnostic scene color, not final ToneMap selection or visual-quality acceptance. Runtime stable/emissive coverage is reported, not assumed.'
    write_json(folder/'report.json', report)
    print(report['status'], report.get('assessment'), report['failures'], flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
