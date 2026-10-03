#!/usr/bin/env python3
import os
import re
import subprocess
import unittest

BIN = os.environ.get("EDGE_ROUTING_PERF_LAB_BIN", "build/edge_routing_perf_lab")


def run(*args):
    return subprocess.run([BIN, *args], text=True, capture_output=True, timeout=30)


class EdgeRoutingPerfTests(unittest.TestCase):
    def test_help_and_invalid_cli(self):
        p = run("--help")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("compare|scale|smoke", p.stdout)
        p = run("--scenario", "invalid")
        self.assertEqual(p.returncode, 2)

    def test_adaptive_overhead_stays_inside_budget(self):
        p = run("--scenario", "compare")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("PHASE53_EDGE_ROUTING_PERF_OK", p.stdout)
        m = re.search(r"OVERHEAD_X1000=(\d+) BUDGET_X1000=(\d+)", p.stdout)
        self.assertIsNotNone(m, p.stdout)
        self.assertLessEqual(int(m.group(1)), int(m.group(2)))
        m = re.search(r"ADAPTIVE_NS_PER_OP=(\d+)\.(\d{3}).*ABSOLUTE_BUDGET_NS=(\d+)", p.stdout)
        self.assertIsNotNone(m, p.stdout)
        adaptive_ns = int(m.group(1)) + int(m.group(2)) / 1000.0
        self.assertLessEqual(adaptive_ns, int(m.group(3)))

    def test_scaling_to_32_backends_remains_bounded(self):
        p = run("--scenario", "scale")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("PHASE53_EDGE_ROUTING_PERF_OK", p.stdout)
        m = re.search(r"GROWTH_X1000=(\d+) GROWTH_BUDGET_X1000=(\d+)", p.stdout)
        self.assertIsNotNone(m, p.stdout)
        self.assertLessEqual(int(m.group(1)), int(m.group(2)))


if __name__ == "__main__":
    unittest.main(verbosity=2)
