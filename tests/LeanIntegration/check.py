"""Compile matching Lean/C/runtime inputs and check re-entry, not performance."""
import argparse
import hashlib
import json
import os
import subprocess
import sys
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


def valid_output(result, instrumented):
    expected = [str(checksum(1000, 2611923443488327891)),
                str(checksum(500, 2611923443488327891))]
    lines = result.stdout.splitlines()
    crossing = 'Lean integration: completed epoch crossing' in result.stderr
    return (result.returncode == 0 and len(lines) == 2
            and all(line.split()[1:] == expected for line in lines)
            and crossing == instrumented
            and result.stderr.count('Lean integration: two callbacks checked') == 1)


def main():
    if not __debug__:
        raise RuntimeError('run without Python optimisation: assertions are test gates')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lean-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path,
                        help='new directory for generated code and raw observations')
    parser.add_argument('--llvm-bin', type=Path)
    args = parser.parse_args()
    lean = args.lean_root.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ)
    for key in list(env):
        if key.startswith(('LEAN_', 'STABILIZER_')):
            del env[key]
    if args.llvm_bin:
        env['PATH'] = str(args.llvm_bin.resolve()) + os.pathsep + env['PATH']
    env.update(LD_LIBRARY_PATH=f'{ROOT}:{lean / "lib/lean"}', LEAN_NUM_THREADS='1',
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
        require_success('generate', [lean / 'bin/lean', '--root=.',
                                    f'--c={out / "Variants.c"}', 'Variants.lean'])
        common = ['-O2', '-I', lean / 'include', '-I', out, HERE / 'driver.c',
                  '-L', lean / 'lib/lean', '-l', 'leanshared', '-l', 'pthread']
        require_success('native-build', ['clang', *common, '-o', out / 'native'])
        require_success('instrumented-build', [sys.executable, ROOT / 'szc', '-v',
                                              '-Rcode', *common, '-o', out / 'instrumented'])
        require_success('instrumentation', ['sh', ROOT / 'tests/Harness/check-instrumentation.sh',
                                            out / 'instrumented'])
        inputs = [HERE / 'Variants.lean', HERE / 'driver.c', Path(__file__),
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
        for variant in VARIANTS:
            for thread in ('0', '1'):
                for binary in ('native', 'instrumented'):
                    env['LEAN_MAIN_USE_THREAD'] = thread
                    result = run(f'{variant}-{thread}-{binary}',
                                 ['timeout', '--kill-after=1s', '10s', out / binary,
                                  variant, '1000', '2611923443488327891'])
                    assert valid_output(result, binary == 'instrumented'), result
                    count += 1
        # Automatic publication uses the same callback and verifies actual
        # counter progress; no guessed sleep stands in for an observed epoch.
        env['STABILIZER_INTERVAL_MS'] = '20'
        for thread in ('0', '1'):
            env['LEAN_MAIN_USE_THREAD'] = thread
            result = run(f'automatic-{thread}', ['timeout', '--kill-after=1s', '10s',
                         out / 'instrumented', 'id-while', '1000', '2611923443488327891'])
            assert valid_output(result, True), result
            count += 1
        # Calibrate the gate: retained startup alone must not satisfy it.
        env.update(STABILIZER_MAX_EPOCHS='1', STABILIZER_INTERVAL_MS='0')
        result = run('no-epoch-negative', ['timeout', '--kill-after=1s', '10s',
                     out / 'instrumented', 'id-while', '1000', '2611923443488327891'])
        assert result.returncode == 1 and 'epoch request rejected' in result.stderr, result
        assert not valid_output(result, True)
        assert before == hashes(), 'inputs changed during checks'
        print(f'PASS: {count} fresh-process checksum/thread/epoch cases and no-epoch rejection')


if __name__ == '__main__':
    main()
