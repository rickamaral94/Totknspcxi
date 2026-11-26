#!/usr/bin/env python3
"""Static regression checks for Android's low-overhead default logging policy."""

from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "runtime/platform/android.c").read_text(encoding="utf-8")

env = source[source.index("static bool driver_log_env(void)") : source.index("static void *driver_log_pump")]
start = source[source.index("static void log_to_logcat(void)") : source.index("// ---- guest address space")]
toggle = start[start.index("void android_driver_logcat(bool on)") :]
pump = source[source.index("static void *driver_log_pump") : source.index("static void log_to_logcat(void)")]

# the per-second driver logcat snapshots: off unless the setting (Driver messages in the log) or the env asks
assert 'getenv("LARECOMP_DRIVER_LOGCAT")' in env and "return e && atoi(e) != 0" in env
assert "if (driver_log_env()) android_driver_logcat(true);" in start
assert "pthread_create(&t, NULL, log_pump" in start  # the runtime logcat relay remains available
assert toggle.index("if (on && !started)") < toggle.index("pthread_create(&t, NULL, driver_log_pump")  # made on first use
assert "if (!atomic_load(&g_driver_logcat_on))" in pump  # turned off again: no logcat started
assert "log capture off" in toggle

print("Android logging policy: runtime relay kept; per-second driver logcat snapshots only on request")
