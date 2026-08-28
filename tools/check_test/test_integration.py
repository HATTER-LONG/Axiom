from __future__ import annotations

import contextlib
import io
import unittest
from unittest.mock import patch

from integration import probe_tools


class ToolProbeTests(unittest.TestCase):
    def test_linux_finds_versioned_mull_tool_pair(self) -> None:
        pair = ("/tools/mull-runner-22", "/tools/mull-ir-frontend-22")
        with (
            patch("integration.platform.system", return_value="Linux"),
            patch("integration.find_mull_tools", return_value=pair),
            patch("integration.shutil.which", return_value=None),
            patch("integration._version", return_value="Mull 0.34.0"),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            tools = probe_tools()

        self.assertTrue(tools["mull-runner"].supported)
        self.assertEqual(tools["mull-runner"].path, pair[0])
        self.assertTrue(tools["mull-ir-frontend"].supported)
        self.assertEqual(tools["mull-ir-frontend"].path, pair[1])


if __name__ == "__main__":
    unittest.main()
