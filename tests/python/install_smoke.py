"""Staged-install smoke test: loads axiom from the relocated prefix and runs a
real discover/invoke cycle through the build-tree test host."""

import os
import sys

extension_dir = os.environ["AXIOM_PYTHON_EXTENSION_DIR"]
sys.path.insert(0, extension_dir)
sys.path.insert(0, os.environ["AXIOM_PYTHON_TEST_HOST_DIR"])
if os.name == "nt":
    runtime_bin = os.environ.get("AXIOM_PYTHON_RUNTIME_BIN")
    if runtime_bin and os.path.isdir(runtime_bin):
        os.add_dll_directory(runtime_bin)

import axiom

assert os.path.realpath(axiom.__file__).startswith(os.path.realpath(extension_dir)), (
    axiom.__file__)

assert str(axiom.ActionId("math.add")) == "math.add"

import axiom_python_test_host

host = axiom_python_test_host.create_host()
namespaces = {module.namespace_name for module in host.runtime.modules()}
assert namespaces == {"math", "mesh"}, namespaces
assert host.runtime.invoke("math.add", {"left": 2, "right": 3}) == 5
assert {str(action.id) for action in host.introspection.actions()} >= {
    "math.add", "mesh.rebuild"}
