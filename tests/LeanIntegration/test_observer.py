"""Deterministic unit gate for the repeated native CPU observer."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lean-root', type=Path, required=True)
    parser.add_argument('--clang', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    clang = args.clang.resolve()
    if shutil.which(str(clang)) is None:
        raise RuntimeError(f'missing clang: {clang}')
    include = args.lean_root.resolve() / 'include'
    observer = HERE / 'repeated-observer.c'
    test = HERE / 'observer-test.c'
    obj = out / 'repeated-observer.o'
    binary = out / 'observer-test'
    commands = [
        ['nice', '-n', '10', 'ionice', '-c', '3', 'timeout', '--kill-after=2s', '30s',
         str(clang), '-std=c11', '-O2', '-Dclock_gettime=observer_test_clock_gettime',
         '-I', str(include), '-c', str(observer), '-o', str(obj)],
        ['nice', '-n', '10', 'ionice', '-c', '3', 'timeout', '--kill-after=2s', '30s',
         str(clang), '-std=c11', '-O2', '-I', str(include), str(test), str(obj),
         '-o', str(binary)],
    ]
    log = out / 'results.jsonl'
    with log.open('x') as handle:
        for command in commands:
            row = subprocess.run(command, text=True, capture_output=True, check=False)
            handle.write(json.dumps({'kind': 'command', 'command': command,
                                     'returncode': row.returncode, 'stdout': row.stdout,
                                     'stderr': row.stderr}) + '\n')
            handle.flush()
            if row.returncode:
                raise RuntimeError(row)
        for mode, expected in [('positive', 0), ('extra', 0), ('maximum', 0),
                               ('fail-0', 1), ('fail-1', 1), ('fail-2', 1), ('fail-3', 1),
                               ('invalid-negative', 1), ('invalid-negative-nsec', 1),
                               ('invalid-nsec', 1), ('invalid-overflow', 1),
                               ('invalid-carry-overflow', 1)]:
            command = ['nice', '-n', '10', 'ionice', '-c', '3', str(binary), mode]
            row = subprocess.run(command, text=True, capture_output=True,
                                 check=False, timeout=10)
            handle.write(json.dumps({'kind': mode, 'command': command,
                                     'returncode': row.returncode,
                                     'stdout': row.stdout, 'stderr': row.stderr}) + '\n')
            handle.flush()
            if row.returncode != expected:
                raise RuntimeError(row)
            diagnostic = ('Adler observer: thread CPU clock failed\n'
                          if mode.startswith('fail-') else
                          'Adler observer: invalid thread CPU time\n' if expected else '')
            if row.stdout or row.stderr != diagnostic:
                raise RuntimeError(row)
    print('PASS: deterministic native observer checks')


if __name__ == '__main__':
    main()
