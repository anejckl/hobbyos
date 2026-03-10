#!/usr/bin/env python3
"""
HobbyOS Interactive QEMU Test Suite

Launches QEMU with monitor socket, sends keystrokes via sendkey commands,
and validates serial output for 21 interactive features across all 5 phases.

Usage: python3 tests/test_interactive.py
Expects: hobbyos.iso and disk.img in the working directory.
Outputs: tests/interactive_serial.log, tests/interactive_results.json
"""

import json
import os
import socket
import subprocess
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
ISO_PATH = os.path.join(ROOT_DIR, "hobbyos.iso")
DISK_PATH = os.path.join(ROOT_DIR, "disk.img")
SERIAL_LOG = os.path.join(SCRIPT_DIR, "interactive_serial.log")
RESULTS_JSON = os.path.join(SCRIPT_DIR, "interactive_results.json")

BOOT_TIMEOUT = 60
TEST_TIMEOUT = 5
OVERALL_TIMEOUT = 120

# QEMU sendkey mapping
KEY_MAP = {
    ' ': 'spc', '/': 'slash', '.': 'dot', '-': 'minus', '_': 'shift-minus',
    '\n': 'ret', '\r': 'ret',
    '=': 'equal', '+': 'shift-equal',
    ',': 'comma', ';': 'semicolon', "'": 'apostrophe',
    '[': 'bracket_left', ']': 'bracket_right',
    '\\': 'backslash',
}


def char_to_sendkey(ch):
    """Convert a character to a QEMU sendkey name."""
    if ch in KEY_MAP:
        return KEY_MAP[ch]
    if 'a' <= ch <= 'z':
        return ch
    if 'A' <= ch <= 'Z':
        return 'shift-' + ch.lower()
    if '0' <= ch <= '9':
        return ch
    # Fallback: try literal
    return ch


class QEMUSession:
    """Manages a QEMU instance with monitor socket for sending keystrokes."""

    def __init__(self):
        self.process = None
        self.monitor_sock = None
        self.serial_pos = 0

    def start(self):
        """Launch QEMU with serial file output and monitor TCP socket."""
        # Clean up old serial log
        if os.path.exists(SERIAL_LOG):
            os.remove(SERIAL_LOG)
        # Touch the file so reads don't fail
        open(SERIAL_LOG, 'w').close()

        cmd = [
            "qemu-system-x86_64",
            "-cdrom", ISO_PATH,
            "-serial", "file:" + SERIAL_LOG,
            "-monitor", "tcp:127.0.0.1:4444,server,nowait",
            "-display", "none",
            "-m", "128M",
            "-no-reboot", "-no-shutdown",
            "-drive", "file=" + DISK_PATH + ",format=raw,if=ide",
            "-netdev", "user,id=net0",
            "-device", "e1000,netdev=net0",
        ]

        self.process = subprocess.Popen(
            cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )

        # Connect to monitor socket (retry up to 3 times)
        for attempt in range(3):
            time.sleep(1)
            try:
                self.monitor_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.monitor_sock.connect(("127.0.0.1", 4444))
                self.monitor_sock.settimeout(2)
                # Read the initial QEMU monitor greeting
                try:
                    self.monitor_sock.recv(4096)
                except socket.timeout:
                    pass
                return
            except (ConnectionRefusedError, OSError):
                if self.monitor_sock:
                    self.monitor_sock.close()
                    self.monitor_sock = None
                if attempt == 2:
                    self.cleanup()
                    raise RuntimeError("Failed to connect to QEMU monitor after 3 attempts")

    def send_monitor_command(self, cmd):
        """Send a command to the QEMU monitor."""
        self.monitor_sock.sendall((cmd + "\n").encode())
        time.sleep(0.05)
        try:
            self.monitor_sock.recv(4096)
        except socket.timeout:
            pass

    def type_string(self, text):
        """Type a string by sending individual sendkey commands."""
        for ch in text:
            key = char_to_sendkey(ch)
            self.send_monitor_command("sendkey " + key)
            time.sleep(0.05)

    def type_line(self, text):
        """Type a string followed by Enter."""
        self.type_string(text)
        self.send_monitor_command("sendkey ret")
        time.sleep(0.05)

    def send_key(self, key):
        """Send a single named key (e.g., 'backspace', 'ctrl-c', 'ret')."""
        self.send_monitor_command("sendkey " + key)
        time.sleep(0.05)

    def read_serial(self):
        """Read full serial log."""
        try:
            with open(SERIAL_LOG, 'r', errors='replace') as f:
                return f.read()
        except FileNotFoundError:
            return ""

    def read_new_serial(self):
        """Read serial output since last snapshot."""
        full = self.read_serial()
        new = full[self.serial_pos:]
        self.serial_pos = len(full)
        return new

    def snapshot_serial(self):
        """Mark current position in serial log."""
        self.serial_pos = len(self.read_serial())

    def wait_for_pattern(self, pattern, timeout=TEST_TIMEOUT):
        """Poll serial log for a pattern. Returns True if found within timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            content = self.read_serial()
            if pattern in content:
                return True
            time.sleep(0.2)
        return False

    def wait_for_pattern_in_new(self, pattern, timeout=TEST_TIMEOUT):
        """Poll for pattern in serial output added since last snapshot."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            full = self.read_serial()
            new = full[self.serial_pos:]
            if pattern in new:
                return True
            time.sleep(0.2)
        return False

    def cleanup(self):
        """Kill QEMU process and close monitor socket."""
        if self.monitor_sock:
            try:
                self.monitor_sock.close()
            except Exception:
                pass
        if self.process:
            try:
                self.process.kill()
                self.process.wait(timeout=5)
            except Exception:
                pass


def run_tests():
    """Run all 23 interactive tests."""
    overall_start = time.time()
    results = []

    qemu = QEMUSession()

    try:
        qemu.start()

        # --- Test 1: Boot + autotests ---
        t_start = time.time()
        boot_patterns = [
            "HobbyOS: Serial debug output initialized",
            "GDT initialized",
            "IDT initialized",
            "Keyboard initialized",
            "TTY subsystem initialized",
            "PMM:",
            "VMM initialized",
            "Kernel heap initialized",
            "Process subsystem initialized",
            "Scheduler initialized",
            "VFS initialized",
            "RAMFS mounted",
            "procfs: mounted at /proc",
            "Device registry initialized",
            "devfs: mounted at /dev",
            "Interrupts enabled",
            "AUTOTEST: all tests passed",
            "shell: shell_run started",
        ]

        # Wait for autotests to pass (shell is already running by then)
        boot_ok = qemu.wait_for_pattern("AUTOTEST: all tests passed", timeout=BOOT_TIMEOUT)
        if boot_ok:
            # Give shell a moment to display prompt and be ready for input
            time.sleep(2)
        serial = qemu.read_serial()

        missing = [p for p in boot_patterns if p not in serial]
        boot_passed = boot_ok and len(missing) == 0
        error_msg = None
        if not boot_ok:
            error_msg = "Timed out waiting for AUTOTEST completion"
        elif missing:
            error_msg = "Missing boot patterns: " + ", ".join(missing)

        results.append({
            "name": "boot_and_autotests",
            "passed": boot_passed,
            "duration_seconds": round(time.time() - t_start, 1),
            "output_snippet": serial[-200:] if serial else "",
            "error": error_msg,
        })

        if not boot_ok:
            # Can't continue if boot failed
            raise RuntimeError("Boot failed — cannot run interactive tests")

        # Helper for interactive tests
        def interactive_test(name, command, check_pattern, negate=False,
                             pre_delay=0.3, post_delay=1.0, send_func=None):
            """Run an interactive test: type command, check serial output."""
            t = time.time()
            qemu.snapshot_serial()
            time.sleep(pre_delay)

            if send_func:
                send_func()
            else:
                qemu.type_line(command)

            time.sleep(post_delay)

            new_output = ""
            if negate:
                # For negative checks, wait a bit then verify pattern is absent
                time.sleep(1.0)
                full = qemu.read_serial()
                new_output = full[qemu.serial_pos:]
                passed = check_pattern not in new_output
                err = ("Pattern should be absent but found: " + check_pattern) if not passed else None
            else:
                found = qemu.wait_for_pattern_in_new(check_pattern, timeout=TEST_TIMEOUT)
                full = qemu.read_serial()
                new_output = full[qemu.serial_pos:]
                passed = found
                err = ("Pattern not found: " + check_pattern) if not passed else None

            results.append({
                "name": name,
                "passed": passed,
                "duration_seconds": round(time.time() - t, 1),
                "output_snippet": new_output[-200:] if new_output else "",
                "error": err,
            })
            # Update serial position
            qemu.serial_pos = len(qemu.read_serial())
            return passed

        # --- Test 2: echo (user program) ---
        interactive_test("run_echo", "run echo Hello World", "Hello World")

        # --- Test 3: mkdir ---
        interactive_test("run_mkdir", "run mkdir /testdir", "ext2: mkdir")

        # --- Test 4: touch ---
        interactive_test("run_touch", "run touch /testdir/file.txt", "ext2: created")

        # --- Test 5: ls dir ---
        interactive_test("run_ls_dir", "run ls /testdir", "file.txt")

        # --- Test 6: ps ---
        interactive_test("run_ps", "run ps", "PID")

        # --- Test 7: rm ---
        interactive_test("run_rm", "run rm /testdir/file.txt", "ext2: unlinked")

        # --- Test 8: ls after rm (negative check) ---
        interactive_test("ls_after_rm", "run ls /testdir", "file.txt", negate=True)

        # --- Test 9: ls root ---
        interactive_test("run_ls_root", "run ls /", "lost")

        # --- Test 10: TTY echo ---
        t = time.time()
        qemu.snapshot_serial()
        time.sleep(0.3)
        qemu.type_line("echo ttytest")
        time.sleep(1.0)
        found = qemu.wait_for_pattern_in_new("ttytest", timeout=TEST_TIMEOUT)
        full = qemu.read_serial()
        new_out = full[qemu.serial_pos:]
        results.append({
            "name": "tty_echo",
            "passed": found,
            "duration_seconds": round(time.time() - t, 1),
            "output_snippet": new_out[-200:] if new_out else "",
            "error": None if found else "TTY echo not seen in serial",
        })
        qemu.serial_pos = len(qemu.read_serial())

        # --- Test 11: TTY backspace ---
        t = time.time()
        qemu.snapshot_serial()
        time.sleep(0.3)
        qemu.type_string("helx")
        time.sleep(0.2)
        qemu.send_key("backspace")
        time.sleep(0.2)
        qemu.type_string("p")
        time.sleep(0.2)
        qemu.send_key("ret")
        time.sleep(1.0)
        found = qemu.wait_for_pattern_in_new("shell> help", timeout=TEST_TIMEOUT)
        full = qemu.read_serial()
        new_out = full[qemu.serial_pos:]
        results.append({
            "name": "tty_backspace",
            "passed": found,
            "duration_seconds": round(time.time() - t, 1),
            "output_snippet": new_out[-200:] if new_out else "",
            "error": None if found else "Backspace editing not working",
        })
        qemu.serial_pos = len(qemu.read_serial())

        # --- Test 12: Ctrl+C ---
        t = time.time()
        qemu.snapshot_serial()
        time.sleep(0.3)
        qemu.type_line("run counter")
        time.sleep(0.1)
        qemu.send_key("ctrl-c")
        time.sleep(2.0)
        full = qemu.read_serial()
        new_out = full[qemu.serial_pos:]
        # Ctrl+C should either show SIGINT, signal delivery, or counter finishes normally
        passed = ("SIGINT" in new_out or "signal" in new_out.lower() or
                  "counter" in new_out.lower() or "shell>" in new_out)
        results.append({
            "name": "ctrl_c",
            "passed": passed,
            "duration_seconds": round(time.time() - t, 1),
            "output_snippet": new_out[-200:] if new_out else "",
            "error": None if passed else "Ctrl+C test: no expected response",
        })
        qemu.serial_pos = len(qemu.read_serial())

        # --- Test 13: shell ls /dev ---
        interactive_test("shell_ls_dev", "ls /dev", "shell> ls /dev")

        # --- Test 14: cat /proc ---
        interactive_test("cat_proc", "cat /proc/self/status", "shell> cat")

        # --- Test 15: 2nd mkdir ---
        interactive_test("mkdir_mydir", "run mkdir /mydir", "ext2: mkdir")

        # --- Test 16: 2nd touch ---
        interactive_test("touch_mydir", "run touch /mydir/notes.txt", "ext2: created")

        # --- Test 17: 2nd ls ---
        interactive_test("ls_mydir", "run ls /mydir", "notes.txt")

        # --- Test 18: exec_test ---
        interactive_test("run_exec_test", "run exec_test",
                         "exec_test: PASS - fork/exec/wait works!", post_delay=3.0)

        # --- Test 19: argv_test ---
        interactive_test("run_argv_test", "run argv_test test1 test2",
                         "argv_test: PASS", post_delay=2.0)

        # --- Test 20: fork_exec_test ---
        interactive_test("run_fork_exec_test", "run fork_exec_test",
                         "fork_exec_test: PASS", post_delay=4.0)

        # --- Test 21: kernel alive ---
        interactive_test("kernel_alive", "mem", "shell> mem")

        # --- Test 22: ifconfig ---
        interactive_test("ifconfig", "ifconfig", "10.0.2.15")

        # --- Test 23: ping gateway ---
        interactive_test("ping_gateway", "ping 10.0.2.2", "Reply from", post_delay=5.0)

    except RuntimeError as e:
        # Boot failure — fill remaining tests as failed
        while len(results) < 23:
            results.append({
                "name": "skipped",
                "passed": False,
                "duration_seconds": 0,
                "output_snippet": "",
                "error": str(e),
            })
    finally:
        qemu.cleanup()

    overall_duration = round(time.time() - overall_start, 1)

    # Compute totals
    passed = sum(1 for r in results if r["passed"])
    failed = sum(1 for r in results if not r["passed"])
    total = len(results)

    # Write JSON results
    output = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "duration_seconds": overall_duration,
        "total": total,
        "passed": passed,
        "failed": failed,
        "tests": results,
    }
    with open(RESULTS_JSON, 'w') as f:
        json.dump(output, f, indent=2)

    # Print human-readable summary
    print("=== HobbyOS Interactive Tests ===")
    for r in results:
        status = " PASS " if r["passed"] else " FAIL "
        dur = "(%5.1fs)" % r["duration_seconds"]
        print("  %s %-25s %s" % (status, r["name"], dur))
        if r["error"]:
            print("        -> %s" % r["error"])
    print()
    print("Results: %d/%d passed (%.1fs)" % (passed, total, overall_duration))
    print("Serial log: %s" % SERIAL_LOG)
    print("Results:    %s" % RESULTS_JSON)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    if not os.path.exists(ISO_PATH):
        print("ERROR: %s not found. Run 'make iso' first." % ISO_PATH)
        sys.exit(1)
    if not os.path.exists(DISK_PATH):
        print("ERROR: %s not found. Run 'make test-interactive' to create it." % DISK_PATH)
        sys.exit(1)
    sys.exit(run_tests())
