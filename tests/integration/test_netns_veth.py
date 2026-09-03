#!/usr/bin/env python3
"""Phase 23 network-namespace/veth integration tests.

The real topology test needs CAP_NET_ADMIN/root because it creates named network
namespaces and a veth pair. Normal `make test` reports an explicit unittest skip
when run unprivileged. The Phase 23 quality gate sets PHASE23_REQUIRE_REAL=1 and
must execute this test with sudo so a skip cannot be mistaken for completion.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "netns_veth_lab.sh"
REQUIRE_REAL = os.environ.get("PHASE23_REQUIRE_REAL") == "1"


def run_lab(command: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    full_env = os.environ.copy()
    if env:
        full_env.update(env)
    return subprocess.run(
        [str(SCRIPT), command],
        cwd=ROOT,
        env=full_env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=15,
        check=False,
    )


class NetnsVethLabTests(unittest.TestCase):
    def test_help_describes_disposable_topology(self) -> None:
        result = run_lab("help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("setup|verify|status|cleanup", result.stdout)
        self.assertIn("veth pair", result.stdout)
        self.assertIn("Exit 77", result.stdout)

    def test_unknown_command_is_rejected(self) -> None:
        result = subprocess.run(
            [str(SCRIPT), "not-a-command"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5,
            check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("Usage:", result.stderr)

    def test_real_network_namespace_topology(self) -> None:
        if os.geteuid() != 0:
            if REQUIRE_REAL:
                self.fail(
                    "PHASE23_REQUIRE_REAL=1 but test is not running as root; "
                    "run: sudo env PHASE23_REQUIRE_REAL=1 "
                    "python3 tests/integration/test_netns_veth.py"
                )
            self.skipTest(
                "real Phase 23 topology requires root/CAP_NET_ADMIN; "
                "the phase gate must rerun this test with sudo"
            )

        suffix = str(os.getpid())[-5:]
        env = {
            "PHASE23_NS_EDGE": f"shp23-edge-{suffix}",
            "PHASE23_NS_BACKEND": f"shp23-back-{suffix}",
            "PHASE23_VETH_EDGE": f"p23e{suffix}",
            "PHASE23_VETH_BACKEND": f"p23b{suffix}",
            "PHASE23_EDGE_CIDR": "10.203.23.1/30",
            "PHASE23_BACKEND_CIDR": "10.203.23.2/30",
        }

        cleanup = run_lab("cleanup", env)
        self.assertEqual(cleanup.returncode, 0, cleanup.stderr)

        try:
            setup = run_lab("setup", env)
            if setup.returncode == 77:
                if REQUIRE_REAL:
                    self.fail(
                        f"kernel/runtime refused required Phase 23 topology: {setup.stderr}"
                    )
                self.skipTest(
                    "real Phase 23 topology unavailable in this runtime; "
                    "the Fedora phase gate must run it successfully with sudo"
                )
            self.assertEqual(setup.returncode, 0, setup.stderr)
            self.assertIn("PHASE23_SETUP_OK", setup.stdout)

            duplicate = run_lab("setup", env)
            self.assertNotEqual(duplicate.returncode, 0)
            self.assertIn("topology already exists", duplicate.stderr)

            status = run_lab("status", env)
            self.assertEqual(status.returncode, 0, status.stderr)
            self.assertIn("10.203.23.1/30", status.stdout)
            self.assertIn("10.203.23.2/30", status.stdout)

            verify = run_lab("verify", env)
            self.assertEqual(verify.returncode, 0, verify.stderr)
            self.assertIn("PING_EDGE_TO_BACKEND=PASS", verify.stdout)
            self.assertIn("PING_BACKEND_TO_EDGE=PASS", verify.stdout)
            self.assertIn("PHASE23_NETNS_VERIFIED", verify.stdout)

            host_net = None
            edge_net = None
            backend_net = None
            for line in verify.stdout.splitlines():
                if line.startswith("HOST_NETNS="):
                    host_net = line.split("=", 1)[1]
                elif line.startswith("EDGE_NETNS="):
                    edge_net = line.split("=", 1)[1]
                elif line.startswith("BACKEND_NETNS="):
                    backend_net = line.split("=", 1)[1]
            self.assertIsNotNone(host_net)
            self.assertIsNotNone(edge_net)
            self.assertIsNotNone(backend_net)
            self.assertEqual(len({host_net, edge_net, backend_net}), 3)
        finally:
            final_cleanup = run_lab("cleanup", env)
            self.assertEqual(final_cleanup.returncode, 0, final_cleanup.stderr)
            second_cleanup = run_lab("cleanup", env)
            self.assertEqual(second_cleanup.returncode, 0, second_cleanup.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
