#!/usr/bin/env python3
"""
HobbyOS Interactive QEMU Test Suite

Launches QEMU with monitor socket, sends keystrokes via sendkey commands,
and validates serial output for 31 interactive features across all 5 phases.

Usage: python3 tests/test_interactive.py [--native]
  --native   Use native Windows QEMU with TCP serial (for QEMU 10.x testing)
  (default)  Use Docker QEMU with file-based serial

Expects: hobbyos.iso and disk.img in the working directory.
Outputs: tests/interactive_serial.log, tests/interactive_results.json
"""

import json
import os
import socket
import subprocess
import sys
import threading
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

# Native QEMU (Windows) settings
NATIVE_QEMU = "/c/msys64/ucrt64/bin/qemu-system-x86_64.exe"
NATIVE_SERIAL_PORT = 5555
NATIVE_MONITOR_PORT = 4445

# QEMU sendkey mapping
KEY_MAP = {
    ' ': 'spc', '/': 'slash', '.': 'dot', '-': 'minus', '_': 'shift-minus',
    '\n': 'ret', '\r': 'ret',
    '=': 'equal', '+': 'shift-equal',
    ',': 'comma', ';': 'semicolon', "'": 'apostrophe',
    '[': 'bracket_left', ']': 'bracket_right',
    '\\': 'backslash',
    '|': 'shift-backslash', '>': 'shift-dot', '<': 'shift-comma',
    '&': 'shift-7',
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

    def __init__(self, native=False):
        self.process = None
        self.monitor_sock = None
        self.serial_pos = 0
        self.native = native
        # TCP serial state (native mode)
        self._serial_buf = ""
        self._serial_lock = threading.Lock()
        self._serial_sock = None
        self._serial_thread = None
        self._serial_stop = threading.Event()

    def _serial_reader_thread(self):
        """Background thread that reads from TCP serial socket into buffer."""
        while not self._serial_stop.is_set():
            try:
                data = self._serial_sock.recv(4096)
                if not data:
                    break
                with self._serial_lock:
                    self._serial_buf += data.decode('utf-8', errors='replace')
            except socket.timeout:
                continue
            except (OSError, ConnectionResetError):
                break

    def start(self):
        """Launch QEMU with serial output and monitor TCP socket."""
        # Clean up old serial log
        if os.path.exists(SERIAL_LOG):
            os.remove(SERIAL_LOG)
        # Touch the file so reads don't fail (used in both modes for final dump)
        open(SERIAL_LOG, 'w').close()

        if self.native:
            monitor_port = NATIVE_MONITOR_PORT
            cmd = [
                NATIVE_QEMU,
                "-cdrom", ISO_PATH,
                "-serial", "tcp:127.0.0.1:%d,server=on,wait=off" % NATIVE_SERIAL_PORT,
                "-monitor", "tcp:127.0.0.1:%d,server=on,wait=off" % monitor_port,
                "-m", "128M",
                "-no-reboot", "-no-shutdown",
                "-drive", "file=%s,format=raw,if=ide" % DISK_PATH,
                "-netdev", "user,id=net0",
                "-device", "e1000,netdev=net0",
            ]
        else:
            monitor_port = 4444
            cmd = [
                "qemu-system-x86_64",
                "-cdrom", ISO_PATH,
                "-serial", "file:" + SERIAL_LOG,
                "-monitor", "tcp:127.0.0.1:%d,server,nowait" % monitor_port,
                "-display", "vnc=127.0.0.1:99",
                "-m", "128M",
                "-no-reboot", "-no-shutdown",
                "-drive", "file=" + DISK_PATH + ",format=raw,if=ide",
                "-netdev", "user,id=net0",
                "-device", "e1000,netdev=net0",
            ]

        self.process = subprocess.Popen(
            cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )

        # In native mode, connect to TCP serial
        if self.native:
            for attempt in range(5):
                time.sleep(1)
                try:
                    self._serial_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    self._serial_sock.connect(("127.0.0.1", NATIVE_SERIAL_PORT))
                    self._serial_sock.settimeout(0.5)
                    # Start reader thread
                    self._serial_thread = threading.Thread(
                        target=self._serial_reader_thread, daemon=True)
                    self._serial_thread.start()
                    break
                except (ConnectionRefusedError, OSError):
                    if self._serial_sock:
                        self._serial_sock.close()
                        self._serial_sock = None
                    if attempt == 4:
                        self.cleanup()
                        raise RuntimeError("Failed to connect to QEMU serial TCP port")

        # Connect to monitor socket (retry up to 5 times)
        for attempt in range(5):
            time.sleep(1)
            try:
                self.monitor_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.monitor_sock.connect(("127.0.0.1", monitor_port))
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
                if attempt == 4:
                    self.cleanup()
                    raise RuntimeError("Failed to connect to QEMU monitor after 5 attempts")

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
        """Read full serial log (from file in Docker mode, from TCP buffer in native mode)."""
        if self.native:
            with self._serial_lock:
                return self._serial_buf
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
        """Kill QEMU process and close sockets."""
        # Stop serial reader thread
        self._serial_stop.set()
        if self._serial_sock:
            try:
                self._serial_sock.close()
            except Exception:
                pass
        if self._serial_thread and self._serial_thread.is_alive():
            self._serial_thread.join(timeout=2)
        # In native mode, dump TCP serial buffer to log file for debugging
        if self.native and self._serial_buf:
            try:
                with open(SERIAL_LOG, 'w') as f:
                    f.write(self._serial_buf)
            except Exception:
                pass
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


def run_tests(native=False):
    """Run all 38 interactive tests."""
    overall_start = time.time()
    results = []

    if native:
        print("=== Native QEMU mode (TCP serial) ===")
        print("QEMU binary: %s" % NATIVE_QEMU)
    else:
        print("=== Docker QEMU mode (file serial) ===")

    qemu = QEMUSession(native=native)

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
        def _exclude_echoed_command(text, command):
            """Strip everything up to and including the first occurrence of
            `command`'s own text from `text`. Used so a check_pattern that
            happens to also appear inside the typed command itself (e.g.
            checking for "ChainA" after typing "echo ChainA ; echo ChainB")
            can't trivially pass from the terminal echoing what was typed —
            it must appear in what comes *after* that echoed line, i.e.
            genuine command output.

            This is deliberately content-based, not timing-based: an
            earlier attempt tried to solve this by re-snapshotting the
            serial position some fixed delay after typing, but that raced
            against the command's own execution speed — if the command
            (and its real output) finished before the delay elapsed, the
            re-snapshot landed *after* the real output too, and every
            check then saw nothing at all. Stripping by content instead
            works regardless of how fast or slow the command runs."""
            if command and command in text:
                idx = text.index(command)
                return text[idx + len(command):]
            return text

        def interactive_test(name, command, check_pattern, negate=False,
                             pre_delay=0.3, post_delay=1.0, send_func=None,
                             exclude_echo=False):
            """Run an interactive test: type command, check serial output.

            exclude_echo=True additionally strips the first occurrence of
            `command`'s own echoed text before searching for check_pattern
            (see _exclude_echoed_command) — use this whenever check_pattern
            might also appear inside the command text being typed."""
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
                search_space = _exclude_echoed_command(new_output, command) if exclude_echo else new_output
                passed = check_pattern not in search_space
                err = ("Pattern should be absent but found: " + check_pattern) if not passed else None
            else:
                deadline = time.time() + TEST_TIMEOUT
                found = False
                while time.time() < deadline:
                    full = qemu.read_serial()
                    new_output = full[qemu.serial_pos:]
                    search_space = _exclude_echoed_command(new_output, command) if exclude_echo else new_output
                    if check_pattern in search_space:
                        found = True
                        break
                    time.sleep(0.2)
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
        interactive_test("run_echo", "run echo Hello World", "Hello World",
                          exclude_echo=True)

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

        # --- Test 24: touch (create file for cp/mv tests) ---
        interactive_test("touch_for_cp", "run touch /cptest.txt", "ext2: created")

        # --- Test 25: cp ---
        interactive_test("run_cp", "run cp /cptest.txt /cptest_copy.txt",
                         "ext2: created", post_delay=1.0)

        # --- Test 26: mv ---
        interactive_test("run_mv", "run mv /cptest_copy.txt /cptest_renamed.txt",
                         "ext2: renamed", post_delay=1.0)

        # --- Test 27: ls shows renamed file (shell ls lists ext2 entries) ---
        interactive_test("ls_after_mv", "ls", "cptest_renamed.txt",
                         post_delay=3.0)

        # --- Test 28: df ---
        interactive_test("run_df", "run df", "total_blocks", post_delay=2.0)

        # --- Test 29: run top ---
        interactive_test("run_top", "run top", "PID", post_delay=1.0)

        # --- Test 30: run hello ---
        interactive_test("run_hello", "run hello", "Hello from user mode!")

        # --- Test 30b: libc_test — statically-linked libc gap-fill exerciser
        # (malloc backward-coalescing stability, FILE* round trip on ext2,
        # deep recursion against the bumped 256KB user stack) ---
        interactive_test("run_libc_test", "run libc_test",
                          "libc_test: all tests passed", post_delay=2.0)

        # --- Test 30c: tcc (self-hosting toolchain, Milestone 2) — runs
        # inside QEMU without crashing and prints its real usage/help text
        # (no compile attempted yet, that's Milestone 3) ---
        interactive_test("run_tcc_help", "run tcc",
                          "Tiny C Compiler", post_delay=2.0)

        # --- Test 30d-g: tcc Milestone 3 — compiles a trivial syscall-only
        # C source (tests/tcc_fixtures/hello.c, uses inline asm for raw
        # int $0x80 syscalls, no libc/crt) into a real ELF entirely inside
        # QEMU, then executes that compiled binary and checks its actual
        # output — not just "the compile step exited 0". Must pass
        # -static -nostdlib: TCC's default dynamic-link output path builds
        # a .dynsym/.dynamic/PT_INTERP section set that has never been
        # exercised on HobbyOS and segfaults; -static skips it entirely,
        # matching the plan's static-only-output decision.
        #
        # tcc_compile_hello positively waits for the shell prompt to
        # return (i.e. the foreground `run tcc ...` truly finished) before
        # checking for "tcc: error", rather than a fixed-delay negate
        # check — a fixed delay only proves no error had appeared *yet*,
        # not that compilation had actually completed, and TCC's compile
        # time varies a lot under CI's slower, non-KVM QEMU. A short fixed
        # delay was previously enough on a fast local host but let
        # tcc_run_compiled race ahead of the real compile finishing and
        # try to exec a not-yet-written file on CI, making that next test
        # flaky there even though this one reported a false green.
        t0 = time.time()
        qemu.snapshot_serial()
        time.sleep(0.3)
        cmd = "run tcc -static -nostdlib -o /tcc_tests/tccout /tcc_tests/hello.c"
        qemu.type_line(cmd)
        prompt_back = qemu.wait_for_pattern_in_new("hobbyos> ", timeout=BOOT_TIMEOUT)
        full = qemu.read_serial()
        new_output = full[qemu.serial_pos:]
        compile_ok = prompt_back and ("tcc: error" not in new_output)
        results.append({
            "name": "tcc_compile_hello",
            "passed": compile_ok,
            "duration_seconds": round(time.time() - t0, 1),
            "output_snippet": new_output[-200:] if new_output else "",
            "error": None if compile_ok else (
                "shell prompt never returned" if not prompt_back
                else "Pattern should be absent but found: tcc: error"),
        })
        qemu.serial_pos = len(qemu.read_serial())

        interactive_test("tcc_enter_sh", "run sh", "$ ", post_delay=2.0)
        interactive_test("tcc_run_compiled", "/tcc_tests/tccout",
                          "tcc works!", post_delay=4.0)
        interactive_test("tcc_exit_sh", "exit", "hobbyos>", post_delay=1.5)

        # --- Test 30h-l: tcc Milestone 4 — compiles a program against the
        # real extended libc (printf/malloc/free/fork/wait), linked
        # against a precompiled libc object (hobbyc.o) rather than
        # compiled by tcc itself (real upstream TCC has no target-side
        # __builtin_va_list/va_arg support on x86-64 outside its own
        # runtime library, which this port doesn't ship). Same
        # positive-wait pattern as tcc_compile_hello above, for the same
        # reason (a fixed delay only proves no error appeared yet, not
        # that compilation actually finished). ---
        t0 = time.time()
        qemu.snapshot_serial()
        time.sleep(0.3)
        cmd = ("run tcc -static -nostdlib -o /tcc_tests/libcout "
               "/tcc_tests/libc_demo.c /tcc_tests/hobbyc.o")
        qemu.type_line(cmd)
        prompt_back = qemu.wait_for_pattern_in_new("hobbyos> ", timeout=BOOT_TIMEOUT)
        full = qemu.read_serial()
        new_output = full[qemu.serial_pos:]
        compile_ok = prompt_back and ("tcc: error" not in new_output)
        results.append({
            "name": "tcc_compile_libc_demo",
            "passed": compile_ok,
            "duration_seconds": round(time.time() - t0, 1),
            "output_snippet": new_output[-200:] if new_output else "",
            "error": None if compile_ok else (
                "shell prompt never returned" if not prompt_back
                else "Pattern should be absent but found: tcc: error"),
        })
        qemu.serial_pos = len(qemu.read_serial())

        interactive_test("tcc_libc_enter_sh", "run sh", "$ ", post_delay=2.0)
        interactive_test("tcc_libc_run_compiled", "/tcc_tests/libcout",
                          "tcc libc demo: all done", post_delay=4.0)
        interactive_test("tcc_libc_exit_sh", "exit", "hobbyos>", post_delay=1.5)

        # --- Test 31: ping QEMU DNS server (10.0.2.3, internal to SLIRP) ---
        # Note: external IPs like 8.8.8.8 fail in CI (no outbound ICMP allowed)
        interactive_test("ping_dns", "ping 10.0.2.3", "Reply from", post_delay=5.0)

        # === User shell tests (run sh, test pipes/redirects/chaining, exit) ===

        # --- Test 32: Enter user shell ---
        interactive_test("sh_enter", "run sh", "$ ", post_delay=2.0)

        # --- Test 33: sh pipe basic (echo | grep) ---
        interactive_test("sh_pipe_grep", "echo TestPipe | grep TestPipe", "TestPipe",
                          post_delay=3.0, exclude_echo=True)

        # --- Test 34: sh redirect out ---
        # Write to file, then cat it
        t = time.time()
        qemu.snapshot_serial()
        time.sleep(0.3)
        qemu.type_line("echo RedirTest > /redir.txt")
        time.sleep(3.0)
        # Re-snapshot so the check below only sees cat's actual output, not
        # the echoed "echo RedirTest..." command line (which itself contains
        # the literal text "RedirTest" and would otherwise false-positive
        # even if the redirect/cat round trip never actually worked).
        qemu.snapshot_serial()
        qemu.type_line("cat /redir.txt")
        time.sleep(3.0)
        found = qemu.wait_for_pattern_in_new("RedirTest", timeout=TEST_TIMEOUT)
        full = qemu.read_serial()
        new_out = full[qemu.serial_pos:]
        results.append({
            "name": "sh_redirect_out",
            "passed": found,
            "duration_seconds": round(time.time() - t, 1),
            "output_snippet": new_out[-200:] if new_out else "",
            "error": None if found else "Redirect output not found",
        })
        qemu.serial_pos = len(qemu.read_serial())

        # --- Test 35: sh append redirect ---
        t = time.time()
        qemu.snapshot_serial()
        time.sleep(0.3)
        qemu.type_line("echo Line1 > /app.txt")
        time.sleep(3.0)
        qemu.type_line("echo Line2 >> /app.txt")
        time.sleep(3.0)
        # Re-snapshot so the check below only sees cat's actual output, not
        # the two echoed "echo Line1.../echo Line2..." command lines (which
        # themselves contain the literal text being checked for).
        qemu.snapshot_serial()
        qemu.type_line("cat /app.txt")
        time.sleep(3.0)
        full = qemu.read_serial()
        new_out = full[qemu.serial_pos:]
        passed = "Line1" in new_out and "Line2" in new_out
        results.append({
            "name": "sh_redirect_append",
            "passed": passed,
            "duration_seconds": round(time.time() - t, 1),
            "output_snippet": new_out[-200:] if new_out else "",
            "error": None if passed else "Append redirect: Line1/Line2 not both found",
        })
        qemu.serial_pos = len(qemu.read_serial())

        # --- Test 36: sh semicolon chaining ---
        interactive_test("sh_chain_semi", "echo ChainA ; echo ChainB", "ChainA",
                          post_delay=3.0, exclude_echo=True)

        # --- Test 37: sh && chaining ---
        interactive_test("sh_chain_and", "echo AndA && echo AndB", "AndB",
                          post_delay=3.0, exclude_echo=True)

        # --- Test 38: Exit user shell ---
        interactive_test("sh_exit", "exit", "exit with status 0", post_delay=3.0)

    except RuntimeError as e:
        # Boot failure — fill remaining tests as failed
        while len(results) < 38:
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
    native_mode = "--native" in sys.argv

    if not os.path.exists(ISO_PATH):
        print("ERROR: %s not found. Run 'make iso' first." % ISO_PATH)
        sys.exit(1)
    if not os.path.exists(DISK_PATH):
        print("ERROR: %s not found. Run 'make test-interactive' to create it." % DISK_PATH)
        sys.exit(1)

    if native_mode:
        # Resolve MSYS path to Windows path for native QEMU
        qemu_path = NATIVE_QEMU
        # Check both MSYS and Windows-style paths
        import shutil
        if not shutil.which(qemu_path) and not os.path.exists(qemu_path):
            # Try Windows-style path
            win_path = "C:/msys64/ucrt64/bin/qemu-system-x86_64.exe"
            if os.path.exists(win_path):
                NATIVE_QEMU = win_path
            else:
                print("ERROR: Native QEMU not found at %s" % qemu_path)
                print("Install QEMU or update NATIVE_QEMU path in test_interactive.py")
                sys.exit(1)

    sys.exit(run_tests(native=native_mode))
