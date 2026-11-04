#!/usr/bin/env python3
"""Standalone versioned P1 gate. Never uses C++ output as the C# expectation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FIX = ROOT / 'docs/evidence/fixtures/p1-v1'
REPORT = ROOT / 'build/reports/p1/differential'
MONO = Path('D:/tools/unity/unity_install/2022.3.62t16/Editor/Data/MonoBleedingEdge')

def call(name, command, output=None):
    (REPORT / (name + '.command.json')).write_text(json.dumps([str(x) for x in command], indent=2) + '\n')
    with (REPORT / (output or name + '.log')).open('w', encoding='utf-8') as log:
        result = subprocess.run([str(x) for x in command], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    (REPORT / (name + '.exit')).write_text(str(result.returncode) + '\n')
    if result.returncode:
        raise RuntimeError(f'{name}: exit {result.returncode}; see report log')

def rows(path):
    records = []
    for line in path.read_text().splitlines():
        record = dict(part.split('=', 1) for part in line.split('|'))
        records.append(record)
    return records

def verify(cpp_path=None, csharp_path=None):
    a, b = rows(cpp_path or REPORT / 'cpp.projection'), rows(csharp_path or REPORT / 'csharp.projection')
    assert len(a) == len(b), f'row count: {len(a)} != {len(b)}'
    index = {}
    chains = {}
    for left, right in zip(a, b):
        case = left['case']
        chain = chains.get(case, 14695981039346656037)
        text = '|'.join(f'{key}={value}' for key, value in left.items() if key != 'hash')
        for byte in text.encode('utf-8'):
            chain = ((chain ^ byte) * 1099511628211) & 0xffffffffffffffff
        assert left['hash'] == f'{chain:016x}', f'hash invalid {case}:{left["tick"]}'
        chains[case] = chain
        assert left.keys() == right.keys(), f'field set {left["case"]}:{left["tick"]}'
        # Compare fields first, NOT merely an equal hash.
        for key in left:
            assert left[key] == right[key], f'first difference case={left["case"]} tick={left["tick"]} field={key}: C++={left[key]} C#={right[key]}'
        index[(left['case'], int(left['tick']))] = left
        if left['case'] == 'three-nodes':
            mask = int(left['mask'])
            choices = list(map(int, left['choices'].split(',')))
            nonzero = [x for x in choices if x]
            assert len(nonzero) == len(set(nonzero)), 'duplicate nonzero candidates'
            # Resolved offers intentionally keep the selected card for replay/reconnect.
            if left['resolved'] == '0':
                for card in nonzero:
                    assert not (mask & (1 << (card - 1))), 'owned candidate'
                    assert card not in (2, 3) or mask & 1, 'hunter prerequisite'
                    assert card not in (5, 6) or mask & 8, 'breach prerequisite'
        if left['case'].endswith('neutral'):
            for k in range(7):
                v = left[f'b{k}'].split(',')
                assert v[0:2] == ['0', '0'] and v[7] == '0', 'Classic build mutation'
    expected = json.loads((FIX / 'expectations.json').read_text())
    for check in expected['checks']:
        row = index[(check['case'], check['tick'])]
        for field, value in check['fields'].items():
            assert row[field] == value, f'hand expectation {check}: actual={row[field]}'
    for check in expected['buildChecks']:
        row = index[('six-cards', check['tick'])]
        value = row[f'b{check["cardIndex"]}'].split(',')[check['column']]
        assert value == check['value'], f'hand build expectation {check}: actual={value}'
    return {'status': 'PASS', 'comparedTicks': len(a), 'comparison': 'all fields + FNV1a64 per-tick chained hash', 'handChecks': len(expected['checks']) + len(expected['buildChecks']), 'cases': sorted({r['case'] for r in a})}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpp-exe', type=Path, help='Use lead-built C++ adapter instead of local g++ build')
    args = parser.parse_args()
    REPORT.mkdir(parents=True, exist_ok=True)
    try:
        spec_text = (FIX / 'input.json').read_text()
        spec = json.loads(spec_text)
        assert spec['schema'] == 'p1-differential-v1' and spec['dtMs'] == 50
        # Adapters deliberately accept a tiny auditable JSON subset: one object/line,
        # compact scalar members. Fail closed instead of silently ignoring reformats.
        object_lines = [line for line in spec_text.splitlines() if line.startswith('{\"kind\"') or line.startswith('{\"tick\"')]
        assert len(object_lines) == len(spec['cases']) + len(spec['requests']) + len(spec['buildEvents'])
        assert all(': ' not in line for line in object_lines), 'compact scalar JSON required'
        cpp = args.cpp_exe.resolve() if args.cpp_exe else REPORT / 'p1-cpp.exe'
        if not args.cpp_exe:
            call('cpp-build', [shutil.which('g++') or 'g++', '-std=c++20', '-O2', '-ffp-contract=off', '-fno-fast-math', '-Wall', '-Wextra', '-Werror', '-I', ROOT / 'server/src', ROOT / 'server/tools/p1-differential.cpp', *[ROOT / f'server/src/expedition/{x}.cpp' for x in ('run', 'encounter', 'build')], ROOT / 'server/src/combat/weapon.cpp', '-o', cpp])
        call('csharp-build', [MONO / 'bin/mono.exe', MONO / 'lib/mono/4.5/csc.exe', '/nologo', '/optimize+', '/out:' + str(REPORT / 'p1-spec.exe'), ROOT / 'tools/p1-differential.cs'])
        call('cpp', [cpp, FIX / 'input.json'], 'cpp.projection')
        call('csharp', [MONO / 'bin/mono.exe', REPORT / 'p1-spec.exe', FIX / 'input.json'], 'csharp.projection')
        summary = verify()
        # Mutate report copies only: both a numeric field and only a hash must fail.
        original = (REPORT / 'csharp.projection').read_text()
        controls = {}
        for name, damaged in [('field', original.replace('|progress=50|', '|progress=51|', 1)),
                              ('hash', original.replace('|hash=', '|hash=0', 1))]:
            path = REPORT / f'negative-{name}.projection'
            path.write_text(damaged)
            try:
                verify(csharp_path=path)
            except AssertionError as error:
                controls[name] = str(error)
                (REPORT / f'negative-{name}.log').write_text(str(error) + '\n')
                (REPORT / f'negative-{name}.exit').write_text('1\n')
            else:
                raise AssertionError(f'negative control {name} was accepted')
        summary['negativeControlsRejected'] = sorted(controls)
        content = (ROOT / 'docs/expedition-p1-content.txt').read_bytes()
        fnv = 2166136261
        for byte in content:
            fnv = ((fnv ^ byte) * 16777619) & 0xffffffff
        assert hashlib.sha256(content).hexdigest() == '22ad0db7b9d1278854d75284e80f376da7758de3388ae0ce096090c8d8a890e7', 'content changed; version input first'
        summary.update(contentSha256=hashlib.sha256(content).hexdigest(), contentFnv1a32=f'{fnv:08x}', inputSha256=hashlib.sha256((FIX / 'input.json').read_bytes()).hexdigest())
        (REPORT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        (REPORT / 'gate.exit').write_text('0\n')
        print(json.dumps(summary, indent=2))
        return 0
    except Exception as error:
        (REPORT / 'gate.exit').write_text('1\n')
        (REPORT / 'failure.log').write_text(str(error) + '\n')
        print(error, file=sys.stderr)
        return 1

if __name__ == '__main__':
    sys.exit(main())
