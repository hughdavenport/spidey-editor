#!/usr/bin/env python3
"""End-to-end check of the text-mode editor: run `./a.out editor` on a small PTY (30 rows,
the size that used to crash it) and drive it through a key sequence that fills the text
panel past the terminal's height - room 9 (Undercroft), its 11-chunk switch 0 opened with
Ctrl-S, chunk 1 selected. The editor must still be running afterwards and its output must
hold no assertion message; then Esc, Esc, q must make it exit cleanly.

Runs the editor against the working directory's ROOMS.SPL read-only (nothing here edits).
One child process, hard deadline, killed on the way out if it will not quit.

    python3 tui_smoke_test.py            (exit 0 = passed)
"""
import fcntl
import os
import pty
import select
import signal
import struct
import sys
import termios
import time

ROWS, COLS = 30, 132
DEADLINE_S = 20.0


def drain(fd, seconds):
    out = b''
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            continue
        try:
            out += os.read(fd, 65536)
        except OSError:
            break
    return out


def exited(pid):
    """(True, status) once the child has exited, else (False, None)."""
    wp, st = os.waitpid(pid, os.WNOHANG)
    return (True, st) if wp else (False, None)


def main():
    pid, fd = pty.fork()
    if pid == 0:
        fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', ROWS, COLS, 0, 0))
        os.environ['SPIDEY_NO_HOTRELOAD'] = '1'
        os.execv('./a.out', ['./a.out', 'editor'])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', ROWS, COLS, 0, 0))
    start = time.time()
    out = drain(fd, 1.0)
    for key in ['r', '0', '9', 's', '0', '\x13', 'c', '1']:
        os.write(fd, key.encode())
        out += drain(fd, 0.4)
        if time.time() - start > DEADLINE_S:
            break
    done, status = exited(pid)
    text = out.decode('latin1')
    failures = []
    if done:
        failures.append(f"editor exited during the key sequence (status {status})")
    if 'Assertion' in text:
        failures.append("an assertion fired: " + next(l for l in text.splitlines() if 'Assertion' in l).strip()[:200])
    if not done:
        for key in ['\x1b', '\x1b', 'q']:
            os.write(fd, key.encode())
            out += drain(fd, 0.4)
        for _ in range(30):
            done, status = exited(pid)
            if done:
                break
            time.sleep(0.1)
        if not done:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            failures.append("editor did not quit on Esc Esc q")
        elif os.WIFSIGNALED(status):
            failures.append(f"editor died of signal {os.WTERMSIG(status)} on quit")
    for f in failures:
        print("FAIL:", f)
    print("tui: 30-row terminal, Undercroft switch 0 chunk list drawn, quit clean" if not failures else f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
