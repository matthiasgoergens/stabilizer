"""Deterministic admission checks, not a statistical layout-diversity gate."""
import os
import resource
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
env = {k: v for k, v in os.environ.items() if not k.startswith('STABILIZER_')}
env['LD_LIBRARY_PATH'] = str(ROOT)
for mode in ('legacy', 'retained'):
    env['STABILIZER_CODE_MODE'] = mode
    for option in (None, '16777216', '1', '', '0', '-1', '1073741825', '999999999999999999999'):
        if option is None:
            env.pop('STABILIZER_MAX_SHUFFLE_BYTES', None)
        else:
            env['STABILIZER_MAX_SHUFFLE_BYTES'] = option
        result = subprocess.run(['timeout', '--kill-after=1s', '10s', HERE / 'build/admission'],
                                env=env, capture_output=True, text=True, check=False)
        if option in (None, '16777216'):
            assert result.returncode == 0 and result.stdout == 'code reservoir admitted\n', result
        else:
            diagnostic = ('cannot provide two slots' if option == '1'
                          else 'invalid STABILIZER_MAX_SHUFFLE_BYTES')
            assert result.returncode != 0 and diagnostic in result.stderr, result
            assert 'code reservoir admitted' not in result.stdout, result
print('PASS: code reservoir admission and invalid budgets in legacy/retained modes')
