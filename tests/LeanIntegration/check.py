"""Compile matching Lean/C/runtime inputs and check re-entry, not performance."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
VERSION = '4.33.1'
REVISION = '819816b2e0a3bf405af45ae5c7af2491d8f5bee6'
VARIANTS = ('id-while', 'io-while', 'for-nat', 'for-usize', 'tail-scalar',
            'tail-pair', 'tail-pair-return', 'while-struct', 'tail-struct', 'tail-fuel')


def checksum(n, seed):
    mask = (1 << 64) - 1
    x = seed
    for i in range(n):
        x = ((x ^ (i + 0x9e3779b97f4a7c15)) * 0xbf58476d1ce4e5b9) & mask
        x ^= x >> 29
        x = (x * 0x94d049bb133111eb) & mask
        x ^= x >> 31
    return x


SEED = 2611923443488327891


def adler_checksum(size, repetitions):
    data = bytearray()
    state = SEED
    for _ in range(size):
        data.append((state // (1 << 24)) % 256)
        state = (1664525 * state + 1013904223) % (1 << 32)
    warm = zlib.adler32(data)
    final = 1
    for _ in range(repetitions):
        final = zlib.adler32(data, final)
    return [final & 65535, final >> 16, warm & 65535, warm >> 16]


def workload_cases(workload):
    if workload == 'loops':
        expected = [checksum(1000, SEED), checksum(500, SEED)]
        return [(variant, [variant, '1000', str(SEED)], expected) for variant in VARIANTS]
    sizes = [(size, reps) for size in (0, 1, 1024) for reps in (0, 1, 3)]
    sizes += [(262144, 8), (1048576, 2)]
    oracle = {(size, reps): adler_checksum(size, reps) for size, reps in sizes}
    return [(f'{kernel}-{size}-{reps}', [kernel, str(size), str(reps), str(SEED)],
             oracle[size, reps]) for kernel in ('helper', 'direct', 'reference')
            for size, reps in sizes]


def valid_output(result, instrumented, expected):
    lines = result.stdout.splitlines()
    crossing = 'Lean integration: completed epoch crossing' in result.stderr
    records = re.findall(r'^Lean integration: code PCs (0x[0-9a-f]+) (0x[0-9a-f]+) '
                         r'(0x[0-9a-f]+) (0x[0-9a-f]+)$', result.stderr, re.MULTILINE)
    if len(records) != 1:
        return False
    pcs = [int(pc, 16) for pc in records[0]]
    exposed = (pcs[0] != pcs[2] and pcs[1] != pcs[3]) if instrumented else (
        pcs[0] == pcs[2] and pcs[1] == pcs[3])
    return (result.returncode == 0 and len(lines) == 2
            and all(len(line.split()) == len(expected) + 1
                    and line.split()[0].isdigit()
                    and line.split()[1:] == list(map(str, expected)) for line in lines)
            and crossing == instrumented
            and all(pcs) and exposed
            and result.stderr.count('Lean integration: two callbacks checked') == 1)


def main():
    if not __debug__:
        raise RuntimeError('run without Python optimisation: assertions are test gates')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lean-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path,
                        help='new directory for generated code and raw observations')
    parser.add_argument('--llvm-bin', type=Path)
    parser.add_argument('--workload', choices=('loops', 'adler'), default='loops')
    args = parser.parse_args()
    lean = args.lean_root.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ)
    env.pop('LD_PRELOAD', None)
    for key in list(env):
        if key.startswith(('LEAN_', 'STABILIZER_')):
            del env[key]
    if args.llvm_bin:
        env['PATH'] = str(args.llvm_bin.resolve()) + os.pathsep + env['PATH']
    env.update(LD_LIBRARY_PATH=f'{out}:{ROOT}:{lean / "lib/lean"}', LEAN_NUM_THREADS='1',
               STABILIZER_CODE_MODE='retained', STABILIZER_MAX_EPOCHS='1000',
               STABILIZER_MAX_CODE_BYTES='67108864', STABILIZER_INTERVAL_MS='0')
    with (out / 'commands.jsonl').open('x') as log:
        def run(name, command):
            command = ['nice', '-n', '10', 'ionice', '-c', '3', 'timeout',
                       '--kill-after=1s', '120s', *map(str, command)]
            result = subprocess.run(command, cwd=HERE, env=env, text=True,
                                    capture_output=True, check=False)
            log.write(json.dumps({
                'name': name, 'command': command, 'cwd': str(HERE),
                'environment': {k: v for k, v in env.items()
                                if k.startswith(('LEAN_', 'STABILIZER_'))
                                or k in ('PATH', 'LD_LIBRARY_PATH')},
                'returncode': result.returncode, 'stdout': result.stdout,
                'stderr': result.stderr}) + '\n')
            log.flush()
            return result

        def require_success(name, command):
            result = run(name, command)
            if result.returncode:
                raise RuntimeError(f'{name}: {result.stderr}')
            return result

        version = require_success('lean-version', [lean / 'bin/lean', '--version'])
        assert f'version {VERSION},' in version.stdout and REVISION in version.stdout
        require_success('clang-version', ['clang', '--version'])
        source = 'Variants.lean' if args.workload == 'loops' else 'AdlerTiming.lean'
        cases = workload_cases(args.workload)
        control = next(case for case in cases if case[0] == (
            'id-while' if args.workload == 'loops' else 'helper-1024-3'))
        require_success('generate', [lean / 'bin/lean', '--root=.',
                                    f'--c={out / "Variants.c"}', source])
        observer = ['clang', '-O2', '-fPIC', '-shared', '-I', lean / 'include',
                    HERE / 'observer.c', '-L', lean / 'lib/lean', '-lleanshared']
        require_success('observer-build', [*observer, '-o', out / 'libobserver.so'])
        fake = out / 'fake-observer'
        fake.mkdir()
        require_success('fake-observer-build', [*observer, '-DLEAN_INTEGRATION_FAKE_PC',
                                              '-o', fake / 'libobserver.so'])
        optimisation = '-O2' if args.workload == 'loops' else '-O3'
        common = [optimisation, '-I', lean / 'include', '-I', out, HERE / 'driver.c',
                  '-L', out, '-lobserver', '-L', lean / 'lib/lean', '-l', 'leanshared', '-l', 'pthread']
        require_success('native-build', ['clang', *common, '-o', out / 'native'])
        require_success('instrumented-build', [sys.executable, ROOT / 'szc', '-v',
                                              '-Rcode', *common, '-o', out / 'instrumented'])
        require_success('instrumentation', ['sh', ROOT / 'tests/Harness/check-instrumentation.sh',
                                            out / 'instrumented'])
        inputs = [HERE / source, HERE / 'driver.c', HERE / 'observer.c', Path(__file__),
                  out / 'libobserver.so', fake / 'libobserver.so',
                  out / 'Variants.c', out / 'native', out / 'instrumented',
                  ROOT / 'LLVMStabilizer.so', ROOT / 'libstabilizer.so',
                  lean / 'bin/lean', *sorted((lean / 'lib/lean').glob('*.so')),
                  lean / 'include/lean/lean.h', lean / 'include/lean/version.h']

        def hashes():
            result = {}
            for path in inputs:
                with path.open('rb') as handle:
                    result[str(path)] = hashlib.file_digest(handle, 'sha256').hexdigest()
            return result

        before = hashes()
        (out / 'inputs.json').write_text(json.dumps(before, indent=2) + '\n')
        count = 0
        for variant, arguments, expected in cases:
            for thread in ('0', '1'):
                for binary in ('native', 'instrumented'):
                    env['LEAN_MAIN_USE_THREAD'] = thread
                    result = run(f'{variant}-{thread}-{binary}',
                                 ['timeout', '--kill-after=1s', '10s', out / binary,
                                  *arguments])
                    assert valid_output(result, binary == 'instrumented', expected), result
                    count += 1
        # Automatic publication uses the same callback and verifies actual
        # counter progress; no guessed sleep stands in for an observed epoch.
        env['STABILIZER_INTERVAL_MS'] = '20'
        for thread in ('0', '1'):
            env['LEAN_MAIN_USE_THREAD'] = thread
            result = run(f'automatic-{thread}', ['timeout', '--kill-after=1s', '10s',
                         out / 'instrumented', *control[1]])
            assert valid_output(result, True, control[2]), result
            count += 1
        # Calibrate the gate: retained startup alone must not satisfy it.
        env.update(STABILIZER_MAX_EPOCHS='1', STABILIZER_INTERVAL_MS='0')
        result = run('no-epoch-negative', ['timeout', '--kill-after=1s', '10s',
                     out / 'instrumented', *control[1]])
        assert result.returncode == 1 and 'epoch request rejected' in result.stderr, result
        assert not valid_output(result, True, control[2])
        # A counter/destination-only gate must not accept a broken observer.
        env.update(STABILIZER_MAX_EPOCHS='1000',
                   LD_LIBRARY_PATH=f'{fake}:{out}:{ROOT}:{lean / "lib/lean"}')
        result = run('fixed-pc-negative', ['timeout', '--kill-after=1s', '10s',
                     out / 'instrumented', *control[1]])
        assert result.returncode == 1 and 'application code PCs did not change' in result.stderr, result
        assert 'Lean integration: completed epoch crossing' in result.stderr, result
        assert not valid_output(result, True, control[2])
        assert before == hashes(), 'inputs changed during checks'
        print(f'PASS: {count} fresh-process checksum/thread/epoch/PC cases and two negative controls')


if __name__ == '__main__':
    main()
