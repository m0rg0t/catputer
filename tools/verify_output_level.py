#!/usr/bin/env python3
"""Compare actual native output levels against an earlier native executable.

Uses temporary WAVs from the shared composer/output stage. Reports digital
levels, not acoustic speaker loudness. Never reads device or saved user state.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as source:
        hasher = hashlib.sha256()
        for chunk in iter(lambda: source.read(128*1024), b''):
            hasher.update(chunk)
        return hasher.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-native', type=Path, required=True)
    parser.add_argument('--native', type=Path, default=ROOT/'build/cmake/lofi_native')
    parser.add_argument('--seconds', type=int, choices=range(30, 181), default=180, metavar='30..180')
    parser.add_argument('--output', type=Path, default=ROOT/'docs/evidence/v0110-output-level.json')
    args = parser.parse_args()
    baseline, current = args.baseline_native.resolve(), args.native.resolve()
    assert baseline != current, 'Use a preserved earlier executable'
    expected_version = (ROOT/'VERSION').read_text().strip()
    rows, failures = [], []
    (ROOT/'build/tmp').mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='output-level-', dir=ROOT/'build/tmp') as folder:
        tmp = Path(folder)
        for mood in ('cozy', 'rainy', 'night', 'sunny'):
            for engine in ('synth', 'hybrid'):
                previous_rms = -1.0
                for volume in (0, 35, 100, 200, 300):
                    pair = []
                    for label, executable in (('before', baseline), ('after', current)):
                        wav, metadata = tmp/f'{label}.wav', tmp/f'{label}.json'
                        subprocess.run([str(executable), '--mood', mood, '--engine', engine,
                                        '--seed', '0xCA7CAFE', '--volume', str(volume),
                                        '--seconds', str(args.seconds), '--wav', str(wav),
                                        '--meta', str(metadata)], check=True, stdout=subprocess.DEVNULL)
                        data = json.loads(metadata.read_text())
                        row = {key: data[key] for key in ('version', 'generation_schema', 'rms', 'peak',
                               'clipped_samples', 'dropped_note_events', 'score_hash', 'score_events',
                               'frames', 'limited_samples')}
                        row['wav_sha256'] = digest(wav)
                        row['rms_dbfs'] = 20*math.log10(data['rms']/32768) if data['rms'] else None
                        pair.append(row)
                    before, after = pair
                    assert after['version'] == expected_version, 'Rebuild the current native executable'
                    gain_db = 20*math.log10(after['rms']/before['rms']) if before['rms'] else None
                    entry = {'mood': mood, 'engine': engine, 'volume': volume,
                             'rms_gain_db': gain_db, 'before': before, 'after': after}
                    rows.append(entry)
                    if (after['clipped_samples'] or after['dropped_note_events'] or after['peak'] > 32700
                        or any(before[key] != after[key] for key in ('generation_schema', 'score_hash', 'score_events', 'frames'))
                        or (volume <= 100 and before['wav_sha256'] != after['wav_sha256'])
                        or after['rms'] < previous_rms
                        or (volume == 300 and (gain_db is None or gain_db < 3.0))):
                        failures.append({'case': len(rows)-1, 'reason': 'output level or compatibility requirement failed'})
                    previous_rms = after['rms']
                print(f'Checked {mood}/{engine}', flush=True)
    report = {'version': expected_version, 'source': 'native_shared_composer_and_output_stage',
              'hardware_verified': False, 'scope': 'Seed 0xCA7CAFE, all four moods/two engines, 0/35/100/200/300% output. Low levels preserve exact WAVs; maximum must raise RMS at least 3 dB. Finite digital evidence, not measured acoustic loudness.',
              'seconds_per_render': args.seconds, 'comparison_count': len(rows),
              'baseline_native_sha256': digest(baseline), 'native_sha256': digest(current),
              'failures': failures, 'comparisons': rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(f'Wrote {args.output}; {len(failures)} failures')
    if failures:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
