"""Self-contained repeated-exposure gate tests; no compiler or saved logs."""
import json
import unittest

from repeated import ORDER, valid

EXPECTED = [10, 20, 30, 40]


def observation(mode='sampled', constant_kernel=False):
    records = []
    for i, label in enumerate(ORDER):
        generation = i // 4 if mode == 'sampled' else 0
        kernel = 'hdr'.index(label.lower())
        pc = 1 if constant_kernel else 0x2000 + 0x1000 * generation + 0x100 * kernel
        records.append({'invocation': i, 'label': label, 'exhausted': 0,
                        'epoch': 0 if mode == 'native' else 1 + generation,
                        'clocks': [hex(0x1000 + generation * 0x100 + offset) for offset in (0, 8)],
                        'kernel_pcs': [hex(pc)] * 4})
    return {'returncode': 0, 'stdout': '0 10 20 30 40\n' * len(ORDER),
            'stderr': '\n'.join(map(json.dumps, records))}


class RepeatedGate(unittest.TestCase):
    def test_positive_modes(self):
        for mode in ('native', 'fixed', 'sampled'):
            with self.subTest(mode=mode):
                self.assertTrue(valid(observation(mode), EXPECTED, 3, mode))

    def test_only_stationary_kernels_satisfy_control(self):
        fake = observation(constant_kernel=True)
        self.assertTrue(valid(fake, EXPECTED, 3, 'sampled', constant_kernel_control=True))
        self.assertFalse(valid(fake, EXPECTED, 3, 'sampled'))
        self.assertFalse(valid(observation(), EXPECTED, 3, 'sampled', constant_kernel_control=True))

    def test_unrelated_failures_do_not_satisfy_control(self):
        for fault in ('checksum', 'missing-record', 'fixed-clock', 'wrong-epoch',
                      'exhausted', 'wrong-label', 'zero-pc', 'failed-process'):
            with self.subTest(fault=fault):
                row = observation(constant_kernel=True)
                records = [json.loads(line) for line in row['stderr'].splitlines()]
                if fault == 'checksum':
                    row['stdout'] = '0 0 0 0 0\n' * len(ORDER)
                elif fault == 'missing-record':
                    records.pop()
                elif fault == 'fixed-clock':
                    for record in records:
                        record['clocks'] = records[0]['clocks']
                elif fault == 'wrong-epoch':
                    records[-1]['epoch'] = 1
                elif fault == 'exhausted':
                    records[-1]['exhausted'] = 1
                elif fault == 'wrong-label':
                    records[-1]['label'] = 'r'
                elif fault == 'zero-pc':
                    records[-1]['kernel_pcs'][0] = '0x0'
                elif fault == 'failed-process':
                    row['returncode'] = 1
                row['stderr'] = '\n'.join(map(json.dumps, records))
                self.assertFalse(valid(row, EXPECTED, 3, 'sampled', constant_kernel_control=True))

    def test_complete_block_order_is_required(self):
        for order in ('', 'hHdr', 'hhhhhhhh', 'hHdrdrHX'):
            with self.subTest(order=order):
                self.assertFalse(valid(observation(), EXPECTED, 3, 'sampled', order=order))

    def test_partial_movement_is_rejected(self):
        row = observation()
        records = [json.loads(line) for line in row['stderr'].splitlines()]
        # Clock PCs and most kernels move, but reference remains at its old site.
        records[5]['kernel_pcs'] = records[3]['kernel_pcs']
        row['stderr'] = '\n'.join(map(json.dumps, records))
        self.assertFalse(valid(row, EXPECTED, 3, 'sampled'))


if __name__ == '__main__':
    unittest.main()
