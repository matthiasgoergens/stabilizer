"""Check real C/C++ frontend IR and code-randomised execution, not timings."""
import os
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = HERE / 'build'
OUT.mkdir(exist_ok=True)
env = dict(os.environ)
env.update(LD_LIBRARY_PATH=str(ROOT), STABILIZER_CODE_MODE='retained',
           STABILIZER_MAX_EPOCHS='1', STABILIZER_MAX_CODE_BYTES='67108864',
           STABILIZER_INTERVAL_MS='0')


def run(command):
    result = subprocess.run(['timeout', '--kill-after=1s', '30s', *map(str, command)],
                            env=env, capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(f'{command}\n{result.stdout}\n{result.stderr}')
    return result.stdout


def attributes(ir, function):
    definition = re.search(r'^define .*@' + function + r'\([^\n]* #([0-9]+)[^\n]* \{',
                           ir, re.MULTILINE)
    if not definition:
        raise RuntimeError(f'missing definition of {function}')
    group = re.search(r'^attributes #' + definition[1] + r' = \{(.*)\}', ir, re.MULTILINE)
    if not group:
        raise RuntimeError(f'missing attributes for {function}')
    return group[1].split()


llvm = Path(run(['llvm-config', '--bindir']).strip())
for suffix in ('c', 'cpp'):
    helper = 'tiny' if suffix == 'c' else '_ZL4tinyi'
    source = OUT / f'fixture.{suffix}'
    source.write_text((HERE / 'fixture.c').read_text())
    for level in range(4):
        name = OUT / f'{suffix}-O{level}'
        raw = Path(str(name) + '.o')
        run([ROOT / 'szc', f'-O{level}', '-c', source, '-o', raw])
        ir = run([llvm / 'llvm-dis', raw, '-o', '-'])
        if level:
            if re.search(r'^define .*@' + helper + r'\(', ir, re.MULTILINE):
                raise RuntimeError(f'{suffix} -O{level}: ordinary helper was not inlined')
        else:
            tiny = attributes(ir, helper)
            if 'noinline' not in tiny or 'optnone' in tiny:
                raise RuntimeError('wrong -O0 attributes for required lowering passes')
        if 'noinline' not in attributes(ir, 'user_noinline'):
            raise RuntimeError('intentional noinline was lost')
        run([ROOT / 'szc', f'-O{level}', '-Rcode', '-v', raw, '-o', name])
        transformed = run([llvm / 'llvm-dis', str(name) + '.opt.bc', '-o', '-'])
        if level and re.search(r'^define .*@' + helper + r'\(', transformed, re.MULTILINE):
            raise RuntimeError('ordinary inline helper survived optimisation')
        if 'noinline' not in attributes(transformed, 'user_noinline'):
            raise RuntimeError('intentional noinline lost during instrumentation')
        run(['sh', ROOT / 'tests/Harness/check-instrumentation.sh', name])
        expected = f'{int(level > 0)} 45\n'
        if run([name]) != expected:
            raise RuntimeError(f'{suffix} -O{level}: wrong optimisation macro or checksum')
    sanitised = OUT / f'{suffix}-asan.o'
    run([ROOT / 'szc', '-O2', '-f', 'sanitize=address', '-c', source, '-o', sanitised])
    asan_ir = run([llvm / 'llvm-dis', sanitised, '-o', '-'])
    if not re.search(r'call void @__asan_report_load4\(', asan_ir):
        raise RuntimeError(f'{suffix}: requested ASan load instrumentation was lost')
print('PASS: C/C++ -O0..3 attributes/execution, explicit noinline and ASan emission')
