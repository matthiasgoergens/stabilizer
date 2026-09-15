"""Build and calibrate repeated Adler exposure; do not analyse timings."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from check import REVISION, SEED, VERSION, adler_checksum

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
ORDER = 'hHdrdrHh'


def valid(row, expected, reps, mode, order=ORDER, constant_kernel_control=False):
    if constant_kernel_control and mode != 'sampled':
        return False
    if len(order) < 8 or len(order) % 4 or any(
            sorted(order[i:i + 4]) != sorted('hHdr') for i in range(0, len(order), 4)):
        return False
    if row['returncode']:
        return False
    lines = row['stdout'].splitlines()
    if len(lines) != len(order):
        return False
    for line in lines:
        fields = line.split()
        if len(fields) != 5 or not fields[0].isdecimal():
            return False
        if fields[1:] != list(map(str, expected)):
            return False
    records = [json.loads(line) for line in row['stderr'].splitlines() if line.startswith('{')]
    if len(records) != len(order):
        return False
    previous = {}
    within = {}
    for i, (record, label) in enumerate(zip(records, order, strict=True)):
        block = i // 4
        before, after = record.get('thread_cpu_before'), record.get('thread_cpu_after')
        if (not isinstance(before, list) or not isinstance(after, list)
                or len(before) != 2 or len(after) != 2
                or any(type(x) is not int or not 0 <= x < 2**64 for x in before + after)
                or not before[0] <= after[0] <= before[1] <= after[1]):
            return False
        if i % 4 == 0:
            previous, within = within, {}
        if (record['invocation'] != i or record['label'] != label or record['exhausted']
                or record['epoch'] != (0 if mode == 'native' else 1 + (
                    block if mode == 'sampled' else 0))):
            return False
        pcs = record['clocks'] + record['kernel_pcs']
        if len(record['clocks']) != 2 or len(record['kernel_pcs']) != reps + 1:
            return False
        if not all(re.fullmatch(r'0x[0-9a-f]+', pc) and int(pc, 16) for pc in pcs):
            return False
        key = label.lower()
        if key in within and pcs != within[key]:
            return False
        if key in previous:
            changed = [a != b for a, b in zip(pcs, previous[key], strict=True)]
            if mode == 'sampled':
                if not all(changed[:2]):
                    return False
                if constant_kernel_control:
                    if any(changed[2:]):
                        return False
                elif not all(changed[2:]):
                    return False
            elif any(changed):
                return False
        within[key] = pcs
    return True


def hashes(paths):
    result = {}
    for path in paths:
        with path.open('rb') as handle:
            result[str(path)] = hashlib.file_digest(handle, 'sha256').hexdigest()
    return result


def main():
    if not __debug__:
        raise RuntimeError('assertions are required')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lean-root', type=Path, required=True)
    parser.add_argument('--llvm-bin', type=Path)
    args = parser.parse_args()
    lean = args.lean_root.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('LEAN_', 'STABILIZER_', 'ADLER_')) and k != 'LD_PRELOAD'}
    if args.llvm_bin:
        env['PATH'] = str(args.llvm_bin.resolve()) + os.pathsep + env['PATH']
    llvm = {}
    for tool in ('clang', 'opt', 'llc'):
        resolved = shutil.which(tool, path=env['PATH'])
        if resolved is None:
            raise RuntimeError(f'missing LLVM tool: {tool}')
        llvm[tool] = Path(resolved)
    env.update(LD_LIBRARY_PATH=f'{out}:{ROOT}:{lean / "lib/lean"}',
               LEAN_NUM_THREADS='1', STABILIZER_CODE_MODE='retained',
               STABILIZER_MAX_EPOCHS='1000', STABILIZER_MAX_CODE_BYTES='67108864',
               STABILIZER_INTERVAL_MS='0', ADLER_ORDER=ORDER)
    source = ROOT / 'tests/LeanIntegration/AdlerTiming.lean'
    inputs = [source, Path(__file__), HERE / 'repeated-driver.c', HERE / 'repeated-observer.c',
              ROOT / 'szc', ROOT / 'libstabilizer.so', ROOT / 'LLVMStabilizer.so',
              lean / 'bin/lean', *sorted((lean / 'lib/lean').glob('*.so')),
              *sorted((lean / 'include/lean').rglob('*.h')),
              *llvm.values(), HERE / 'check.py']
    before = hashes(inputs)
    (out / 'inputs.json').write_text(json.dumps(before, indent=2) + '\n')
    with (out / 'commands.jsonl').open('x') as log:
        def run(name, command):
            cmd = ['nice', '-n', '10', 'ionice', '-c', '3', 'timeout',
                   '--kill-after=2s', '120s', *map(str, command)]
            p = subprocess.run(cmd, cwd=ROOT, env=env, text=True, capture_output=True, check=False)
            row = {'name': name, 'command': cmd, 'cwd': str(ROOT), 'returncode': p.returncode,
                   'stdout': p.stdout, 'stderr': p.stderr,
                   'environment': {k: v for k, v in env.items()
                                   if k.startswith(('LEAN_', 'STABILIZER_', 'ADLER_'))
                                   or k in ('PATH', 'LD_LIBRARY_PATH')}}
            log.write(json.dumps(row) + '\n')
            log.flush()
            return row

        def require(name, command):
            row = run(name, command)
            if row['returncode']:
                raise RuntimeError(row)
            return row

        version = require('lean-version', [lean / 'bin/lean', '--version'])['stdout']
        assert f'version {VERSION},' in version and REVISION in version
        require('generate', [lean / 'bin/lean', f'--c={out / "original.c"}', source])
        generated = (out / 'original.c').read_text()
        for tag, name in enumerate(('helper', 'directHelper', 'reference'), 1):
            pattern = rf'^(LEAN_EXPORT lean_object\* l_AdlerTiming_{name}\([^;\n]*\)\{{)$'
            generated, count = re.subn(pattern, rf'\1\n adler_kernel_probe({tag});',
                                       generated, flags=re.MULTILINE)
            assert count == 1, (name, count)
        (out / 'Adler.c').write_text(generated)
        observer = [llvm['clang'], '-O2', '-fPIC', '-shared', '-I', lean / 'include',
                    HERE / 'repeated-observer.c', '-L', lean / 'lib/lean', '-lleanshared']
        require('observer', [*observer, '-o', out / 'librepeated.so'])
        fake = out / 'fake'
        fake.mkdir()
        require('fake-observer', [*observer, '-DADLER_FAKE_PC', '-o', fake / 'librepeated.so'])
        common = ['-O3', '-I', lean / 'include', '-I', out, HERE / 'repeated-driver.c',
                  '-L', out, '-lrepeated', '-L', lean / 'lib/lean', '-lleanshared', '-lpthread']
        require('native', [llvm['clang'], *common, '-o', out / 'native'])
        require('instrumented', [sys.executable, ROOT / 'szc', '-v', '-Rcode',
                                 *common, '-o', out / 'instrumented'])
        require('instrumentation', ['sh', ROOT / 'tests/Harness/check-instrumentation.sh',
                                    out / 'instrumented'])
        built = [out / p for p in ('original.c', 'Adler.c', 'native', 'instrumented',
                                   'librepeated.so', 'fake/librepeated.so')]
        built_hashes = hashes(built)
        (out / 'build.json').write_text(json.dumps(built_hashes, indent=2) + '\n')
        for mode in ('native', 'fixed', 'sampled'):
            env['ADLER_MODE'] = mode
            binary = out / ('native' if mode == 'native' else 'instrumented')
            for thread in ('0', '1'):
                env['LEAN_MAIN_USE_THREAD'] = thread
                for size, reps in ((0, 0), (1024, 3), (262144, 8), (1048576, 2)):
                    row = run(f'{mode}-{thread}-{size}-{reps}',
                              [binary, 'helper', size, reps, SEED])
                    assert valid(row, adler_checksum(size, reps), reps, mode), row
        env.update(ADLER_MODE='native', ADLER_ORDER='hhhhhhhh')
        row = run('invalid-order-negative', [out / 'native', 'helper', 1024, 3, SEED])
        assert row['returncode'] == 1 and row['stdout'] == '', row
        assert 'Adler repeated: block must contain h H d r once each' in row['stderr'], row
        assert not any(line.startswith('{') for line in row['stderr'].splitlines()), row
        env.update(ADLER_MODE='sampled', ADLER_ORDER=ORDER)
        env['STABILIZER_MAX_EPOCHS'] = '1'
        row = run('exhaustion-negative', [out / 'instrumented', 'helper', 1024, 3, SEED])
        assert row['returncode'] == 1 and row['stdout'] == '', row
        assert row['stderr'].count('Adler repeated: exhausted before first callback') == 1, row
        assert not any(line.startswith('{') for line in row['stderr'].splitlines()), row
        env.update(STABILIZER_MAX_EPOCHS='1000',
                   LD_LIBRARY_PATH=f'{fake}:{out}:{ROOT}:{lean / "lib/lean"}')
        row = run('fixed-kernel-pc-negative', [out / 'instrumented', 'helper', 1024, 3, SEED])
        assert row['returncode'] == 0, row
        assert valid(row, adler_checksum(1024, 3), 3, 'sampled', constant_kernel_control=True), row
        assert not valid(row, adler_checksum(1024, 3), 3, 'sampled'), row
    after = hashes(inputs)
    (out / 'inputs-after.json').write_text(json.dumps(after, indent=2) + '\n')
    assert before == after and hashes(built) == built_hashes, 'inputs changed'
    print('PASS: 24 repeated-process checks (192 invocations), three negative controls; no timing analysis')


if __name__ == '__main__':
    main()
