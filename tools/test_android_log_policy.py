#!/usr/bin/env python3
"""Static regression checks for Android's low-overhead default logging policy."""

from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "runtime/platform/android.c").read_text(encoding="utf-8")

helper = source[source.index("static bool driver_log_capture_enabled(void)") : source.index("static void *driver_log_pump")]
start = source[source.index("static void log_to_logcat(void)") : source.index("// ---- guest address space")]

assert 'getenv("LARECOMP_DRIVER_LOGCAT")' in helper
assert "return e && atoi(e) != 0" in helper
gate = start.index("if (driver_log_capture_enabled())")
worker = start.index("pthread_create(&t, NULL, driver_log_pump")
assert gate < worker
assert "pthread_create(&t, NULL, log_pump" in start  # the runtime logcat relay remains available
assert "log capture off" in start

print("Android logging policy: runtime relay kept; per-second driver logcat snapshots are explicit opt-in")
