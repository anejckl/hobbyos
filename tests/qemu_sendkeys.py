#!/usr/bin/env python3
"""Send commands to QEMU via monitor TCP socket using sendkey."""

import socket
import time
import sys

MONITOR_HOST = "127.0.0.1"
MONITOR_PORT = 4444

# QEMU sendkey name mapping for special/shifted characters
SHIFT_MAP = {
    '!': '1', '@': '2', '#': '3', '$': '4', '%': '5',
    '^': '6', '&': '7', '*': '8', '(': '9', ')': '0',
    '_': 'minus', '+': 'equal', '{': 'bracket_left',
    '}': 'bracket_right', '|': 'backslash', ':': 'semicolon',
    '"': 'apostrophe', '<': 'comma', '>': 'dot', '?': 'slash',
    '~': 'grave_accent',
}

KEY_MAP = {
    ' ': 'spc', '-': 'minus', '=': 'equal', '[': 'bracket_left',
    ']': 'bracket_right', '\\': 'backslash', ';': 'semicolon',
    "'": 'apostrophe', ',': 'comma', '.': 'dot', '/': 'slash',
    '`': 'grave_accent', '\t': 'tab', '\n': 'ret',
}


def send_monitor(sock, cmd):
    """Send a command to QEMU monitor and read response."""
    sock.sendall((cmd + "\n").encode())
    time.sleep(0.05)


def char_to_sendkey(ch):
    """Convert a character to QEMU sendkey syntax."""
    if ch in SHIFT_MAP:
        return f"shift-{SHIFT_MAP[ch]}"
    if ch in KEY_MAP:
        return KEY_MAP[ch]
    if ch.isupper():
        return f"shift-{ch.lower()}"
    if ch.isalnum():
        return ch
    return None


def send_string(sock, text, delay=0.05):
    """Type a string into QEMU one character at a time."""
    for ch in text:
        key = char_to_sendkey(ch)
        if key:
            send_monitor(sock, f"sendkey {key}")
            time.sleep(delay)


def send_command(sock, cmd, delay=0.05):
    """Type a command and press Enter."""
    send_string(sock, cmd, delay)
    time.sleep(0.1)
    send_monitor(sock, "sendkey ret")


def connect_monitor():
    """Connect to QEMU monitor."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((MONITOR_HOST, MONITOR_PORT))
    sock.settimeout(2)
    # Read the initial banner
    try:
        sock.recv(4096)
    except socket.timeout:
        pass
    return sock


def main():
    print("Connecting to QEMU monitor...")
    sock = connect_monitor()
    print("Connected!\n")

    # Test sequence with wait times
    tests = [
        ("Wait for boot", None, 15),
        ("run echo Hello World", "run echo Hello World", 3),
        ("run sh (enter user shell)", "run sh", 3),
        ("echo Hello | grep Hello", "echo Hello | grep Hello", 3),
        ("echo RedirTest > /redir.txt", "echo RedirTest > /redir.txt", 2),
        ("cat /redir.txt", "cat /redir.txt", 2),
        ("echo Line1 > /app.txt", "echo Line1 > /app.txt", 2),
        ("echo Line2 >> /app.txt", "echo Line2 >> /app.txt", 2),
        ("cat /app.txt (expect both lines)", "cat /app.txt", 2),
        ("echo ChainA ; echo ChainB", "echo ChainA ; echo ChainB", 2),
        ("echo AndA && echo AndB", "echo AndA && echo AndB", 2),
        ("exit (back to kernel shell)", "exit", 3),
    ]

    for desc, cmd, wait in tests:
        if cmd is None:
            print(f"--- {desc} ({wait}s) ---")
            time.sleep(wait)
        else:
            print(f">>> {desc}")
            send_command(sock, cmd)
            time.sleep(wait)

    print("\n=== All commands sent! Check QEMU GUI and serial output. ===")
    sock.close()


if __name__ == "__main__":
    main()
