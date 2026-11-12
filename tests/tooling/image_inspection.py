#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv.pop(1)).resolve()
ROOT = pathlib.Path(__file__).resolve().parents[2]

class Inspection(unittest.TestCase):
    def test_validated_summary_graph_and_bounds(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = pathlib.Path(temporary) / 'hello.nvr'
            build = subprocess.run([str(NEURA), 'build', str(ROOT / 'examples/hello-world/hello-world.nra'), '-o', str(image)], capture_output=True)
            self.assertEqual(build.returncode, 0, build.stderr.decode())
            summary = subprocess.run([str(NEURA), 'inspect-image', '--json', str(image)], capture_output=True)
            self.assertEqual(summary.returncode, 0, summary.stderr.decode())
            data = json.loads(summary.stdout); self.assertEqual(data['version'], 1)
            self.assertTrue(data['functions']); self.assertTrue(data['graphs']); self.assertTrue(data['sources'])
            detail = subprocess.run([str(NEURA), 'inspect-image', '--json', str(image), '--graph', '0', '--limit', '2'], capture_output=True)
            self.assertEqual(detail.returncode, 0, detail.stderr.decode())
            graph = json.loads(detail.stdout)['graph']; self.assertLessEqual(len(graph['neurons']), 2)
            for neuron in graph['neurons']:
                self.assertEqual(neuron['wire'], graph['inputLanes'] + neuron['id']); self.assertIsInstance(neuron['bias'], str)
            for edge in graph['edges']: self.assertIsInstance(edge['weight'], str)
            invalid = subprocess.run([str(NEURA), 'inspect-image', '--json', str(image), '--graph', str(len(data['graphs']))], capture_output=True)
            self.assertNotEqual(invalid.returncode, 0)

if __name__ == '__main__': unittest.main()
