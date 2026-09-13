"""Cheap output-gate regressions; no Lean compiler or runtime required."""
import subprocess
import unittest

from check import valid_output


class OutputGate(unittest.TestCase):
    def result(self, elapsed='0', instrumented=True):
        pcs = '0x1 0x2 0x3 0x4' if instrumented else '0x1 0x2 0x1 0x2'
        crossing = 'Lean integration: completed epoch crossing\n' if instrumented else ''
        return subprocess.CompletedProcess(
            [], 0, stdout=f'{elapsed} 1 0 1 0\n' * 2,
            stderr=crossing + f'Lean integration: code PCs {pcs}\n'
            'Lean integration: two callbacks checked\n')

    def test_zero_elapsed_is_valid(self):
        for instrumented in (False, True):
            with self.subTest(instrumented=instrumented):
                self.assertTrue(valid_output(self.result(instrumented=instrumented),
                                             instrumented, [1, 0, 1, 0]))

    def test_malformed_elapsed_is_rejected(self):
        for elapsed in ('-1', 'nan', '', '1.0'):
            with self.subTest(elapsed=elapsed):
                self.assertFalse(valid_output(self.result(elapsed), True, [1, 0, 1, 0]))

    def test_wrong_checksum_is_rejected(self):
        self.assertFalse(valid_output(self.result('1'), True, [2, 0, 1, 0]))

    def test_unchanged_pc_is_rejected(self):
        result = self.result('1')
        result.stderr = result.stderr.replace('0x3 0x4', '0x1 0x2')
        self.assertFalse(valid_output(result, True, [1, 0, 1, 0]))


if __name__ == '__main__':
    unittest.main()
