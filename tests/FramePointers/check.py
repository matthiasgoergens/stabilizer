"""Check frame attributes and emitted leaf prologues without running a walker."""
import re
import resource
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = HERE / 'build'
OUT.mkdir(exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(cmd, failure=None):
    result = subprocess.run(['timeout', '--kill-after=1s', '30s', *map(str, cmd)],
                            capture_output=True, text=True, check=False)
    if failure is None:
        if result.returncode:
            raise RuntimeError(f'{cmd}\n{result.stdout}\n{result.stderr}')
    elif result.returncode == 0 or failure not in result.stderr:
        raise RuntimeError(f'missing diagnostic {failure}: {result}')
    return result.stdout


llvm = Path(run(['llvm-config', '--bindir']).strip())
plugin = '--load-pass-plugin=' + str(ROOT / 'LLVMStabilizer.so')


def frame_attr(ir, name):
    line = re.search(r'^(?:define|declare) .*@' + name + r'\([^\n]*', ir, re.MULTILINE)
    if not line:
        raise RuntimeError(f'missing function {name}')
    group = re.search(r'#(\d+)', line[0])
    if not group:
        return None
    attrs = re.search(r'^attributes #' + group[1] + r' = \{(.*)\}', ir, re.MULTILINE)
    if not attrs:
        raise RuntimeError('missing attribute group')
    frame = re.search(r'"frame-pointer"="([^"]+)"', attrs[1])
    return frame[1] if frame else None


expected = {'leaf_none': 'none', 'leaf_nonleaf': 'non-leaf',
            'leaf_all': 'all', 'leaf_unspecified': None, 'external': 'none'}
for mode in ('heap', 'code'):
    output = OUT / f'{mode}.ll'
    run([llvm / 'opt', plugin, '-passes=stabilize', f'-stabilize-{mode}=true',
         HERE / 'frames.ll', '-S', '-o', output])
    ir = output.read_text()
    for name, original in expected.items():
        wanted = 'all' if mode == 'code' and name != 'external' else original
        if frame_attr(ir, name) != wanted:
            raise RuntimeError(f'{mode}: {name} frame policy differs from {wanted}')

# A conflicting backend default cannot undo the pass's explicit attributes.
assembly = run([llvm / 'llc', '-O2', '--frame-pointer=none', OUT / 'code.ll', '-o', '-'])
for name in expected:
    if name == 'external':
        continue
    body = re.search(r'^' + name + r':(.*?)^\.Lfunc_end', assembly, re.MULTILINE | re.DOTALL)
    if not body or not re.search(r'pushq\s+%rbp', body[1]) or not re.search(r'movq\s+%rsp, %rbp', body[1]):
        raise RuntimeError(f'{name}: no emitted frame prologue')

# Calibrate the gate: all on the command line alone is insufficient.
control = run([llvm / 'llc', '-O2', '--frame-pointer=all', HERE / 'frames.ll', '-o', '-'])
body = re.search(r'^leaf_none:(.*?)^\.Lfunc_end', control, re.MULTILINE | re.DOTALL)
if not body or re.search(r'pushq\s+%rbp', body[1]):
    raise RuntimeError('conflicting-attribute negative control changed')
run([llvm / 'opt', plugin, '-passes=stabilize', '-stabilize-code=true',
     HERE / 'naked.ll', '--disable-output'], failure='does not support naked functions')
# This code-only policy must not reject a heap-only module for its naked body.
run([llvm / 'opt', plugin, '-passes=stabilize', '-stabilize-heap=true',
     HERE / 'naked.ll', '--disable-output'])
print('PASS: code frame attributes/prologues, heap/declaration controls and naked diagnostic')
