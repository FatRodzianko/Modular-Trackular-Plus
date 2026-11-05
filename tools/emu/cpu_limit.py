"""Keep the tests under 75 % of the machine's CPU, on the user's standing rule.

Every test that imports machine.py is held to the same few CPUs, and so are its children, so tests
started separately share them instead of adding up. One CPU comes off the 75 % for what else is
running: 5 of 8, and the machine read 81 % with six jobs and its usual load.

MT_TEST_CPUS=<n> changes the number. Use it only when the user has said to go over the limit.
"""
import ctypes
import os


def allowed():
    """How many CPUs the tests may use."""
    if os.environ.get("MT_TEST_CPUS"):
        return max(1, int(os.environ["MT_TEST_CPUS"]))
    return max(1, (os.cpu_count() or 1) * 3 // 4 - 1)


def apply():
    """Hold this process and whatever it starts to those CPUs. Stops the test if it can't."""
    n = allowed()
    if hasattr(os, "sched_setaffinity"):
        os.sched_setaffinity(0, set(sorted(os.sched_getaffinity(0))[:n]))
        return
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetCurrentProcess.restype = ctypes.c_void_p
    kernel32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    if not kernel32.SetProcessAffinityMask(kernel32.GetCurrentProcess(), (1 << n) - 1):
        raise OSError(ctypes.get_last_error(), "couldn't limit the tests' CPU use")
