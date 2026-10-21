#!/usr/bin/env python3
"""End-to-end check of the text-mode editor: run `./a.out tui` on a small PTY (30 rows,
the size that used to crash it) and drive it through a key sequence that fills the text
panel past the terminal's height - room 1 (Switch Demo), whose out-of-bounds switch and its
chunk are listed under the map. The editor must still be running afterwards and its output must
hold no assertion message; then Esc, Esc, q must make it exit cleanly.

Runs on the demo data set (docs/demo: gen_demo.py's graphics, rooms.txt imported) built in a
temp dir handed to the editor with --data-dir, so it needs none of the game's files and
nothing typed here can touch the live ones. One child process, hard deadline, killed on the
way out if it will not quit.

    python3 tui_smoke_test.py            (exit 0 = passed)
"""
import fcntl
import os
import shutil
import tempfile
import pty
import re
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


def drain_until_quiet(fd, timeout, quiet=0.4):
    """Read until nothing more arrives for `quiet` seconds (or `timeout` in all): a whole frame."""
    out = b''
    end = time.time() + timeout
    last = time.time()
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                out += os.read(fd, 65536)
            except OSError:
                break
            last = time.time()
        elif out and time.time() - last > quiet:
            break
    return out


def exited(pid):
    """(True, status) once the child has exited, else (False, None)."""
    wp, st = os.waitpid(pid, os.WNOHANG)
    return (True, st) if wp else (False, None)


class Screen:
    """A minimal VT parser: enough of CUP / SGR / newline to rebuild what the editor drew."""
    SEQ = re.compile(r'\x1b\[([0-9;]*)([A-Za-z])|\x1b[^\[]|\x1b\[\?[0-9;]*[a-z]|(.)', re.S)

    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.cells = [[' '] * cols for _ in range(rows)]
        self.bg = [[0] * cols for _ in range(rows)]
        self.x = self.y = 0
        self.cur_bg = 0

    def feed(self, text):
        for m in self.SEQ.finditer(text):
            if m.group(2):
                params = [int(p) if p else 0 for p in m.group(1).split(';')] if m.group(1) else []
                if m.group(2) == 'H':
                    self.y = (params[0] if params else 1) - 1
                    self.x = (params[1] if len(params) > 1 else 1) - 1
                elif m.group(2) == 'm':
                    if not params or params == [0]:
                        self.cur_bg = 0
                    for p in params:
                        if p == 0:
                            self.cur_bg = 0
                        elif 40 <= p <= 47:
                            self.cur_bg = p
            elif m.group(3):
                c = m.group(3)
                if c == '\n':
                    self.y += 1; self.x = 0
                elif c == '\r':
                    self.x = 0
                elif c >= ' ':
                    if 0 <= self.y < self.rows and 0 <= self.x < self.cols:
                        self.cells[self.y][self.x] = c
                        self.bg[self.y][self.x] = self.cur_bg
                    self.x += 1

    def row(self, y):
        return ''.join(self.cells[y])

    def text(self):
        return '\n'.join(self.row(y) for y in range(self.rows))


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'docs', 'demo'))
import gen_demo  # noqa: E402
import spidey_gfx  # noqa: E402


def backs_block(sandbox, bid):
    """Row 0 of BACKS.SPL block `bid` as the hex the editor should show (spidey_gfx's decoder)."""
    tiles = spidey_gfx.backs_tilemap(open(os.path.join(sandbox, 'BACKS.SPL'), 'rb').read(), bid)
    return ''.join(f'{v:02X}' if v else '  ' for v in tiles[:32])


def main():
    failures = []
    sandbox = tempfile.mkdtemp(prefix='spidey-tui-smoke.')
    gen_demo.main(sandbox)
    import subprocess
    subprocess.run([os.path.abspath('a.out'), 'import', os.path.join(HERE, 'docs', 'demo', 'rooms.txt'), 'ROOMS.SPL'],
                   cwd=sandbox, check=True, capture_output=True, timeout=60)
    pid, fd = pty.fork()
    if pid == 0:
        fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', ROWS, COLS, 0, 0))
        os.environ['SPIDEY_NO_HOTRELOAD'] = '1'
        os.execv('./a.out', ['./a.out', '--data-dir', sandbox, 'tui'])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', ROWS, COLS, 0, 0))
    start = time.time()
    rooms_before = open(os.path.join(sandbox, 'ROOMS.SPL'), 'rb').read()
    out = drain_until_quiet(fd, 40.0, quiet=0.8)   # long enough for the editor to build editor.so if it must
    # The TUI starts with its help box up; on 30 rows the box does not fit, so it must be
    # clipped with a PgUp/PgDn note rather than drawn off the top. Then hide it (Ctrl-?), so
    # PgDn below scrolls the panel and not the help.
    scr = Screen(ROWS, COLS); scr.feed(out.decode('latin1'))
    if 'help' not in scr.text() or 'PgUp/PgDn:' not in scr.text():
        failures.append(f"help box on a 30-row terminal is not shown clipped with a PgUp/PgDn note (row 0 {scr.row(0).rstrip()[:40]!r})")
    os.write(fd, b'\x1f'); more = drain(fd, 0.6); out += more; scr.feed(more.decode('latin1'))
    for key in ['r', '0', '1']:   # Switch Demo: its out-of-bounds switch and chunk are listed under a 30-row screen
        os.write(fd, key.encode())
        more = drain(fd, 0.4); out += more; scr.feed(more.decode('latin1'))
        if time.time() - start > DEADLINE_S:
            break
    done, status = exited(pid)
    text = out.decode('latin1')
    if done:
        failures.append(f"editor exited during the key sequence (status {status})")
    if 'Assertion' in text:
        failures.append("an assertion fired: " + next(l for l in text.splitlines() if 'Assertion' in l).strip()[:200])
    if not done:
        # PgDn scrolls the panel: the header's right end says so and the frame changes
        before = scr.text()
        os.write(fd, b'\x1b[6~'); more = drain(fd, 0.6); text += more.decode('latin1')
        scr.feed(more.decode('latin1'))
        if 'panel +' not in scr.row(0):
            failures.append("PgDn did not scroll the panel (no 'panel +N' in the header)")
        if scr.text() == before:
            failures.append("PgDn changed nothing on screen")
        if 'type block' not in scr.text():
            failures.append("the switch's chunk list did not scroll into view")
        # Ctrl-B: the background block's hex replaces the map; Esc brings the room back
        os.write(fd, b'r'); drain(fd, 0.3); os.write(fd, b'01'); more = drain(fd, 0.6); scr.feed(more.decode('latin1'))
        os.write(fd, b'\x02'); more = drain(fd, 0.8); scr.feed(more.decode('latin1'))
        expect = backs_block(sandbox, 1)
        if scr.row(1)[:64] != expect:
            failures.append(f"Ctrl-B map row 0 is {scr.row(1)[:64]!r}, BACKS.SPL block 1 says {expect!r}")
        if 'BACKGROUND 01' not in scr.text():
            failures.append("Ctrl-B panel does not name the background block")
        # s 02 inside the view: another sheet, shown with its own room list
        os.write(fd, b's'); drain(fd, 0.3); os.write(fd, b'02'); more = drain(fd, 0.8); scr.feed(more.decode('latin1'))
        if 'BACKGROUND 02' not in scr.text() or 'used by rooms:' not in scr.text():
            failures.append("s 02 in the background view did not switch to sheet 02")
        expect2 = backs_block(sandbox, 2)
        if scr.row(1)[:64] != expect2:
            failures.append(f"sheet 02 map row 0 is {scr.row(1)[:64]!r}, BACKS.SPL block 2 says {expect2!r}")
        # r 03 inside the view: the room prompt works and the view comes back for Rooftop (its own block again)
        os.write(fd, b'r'); drain(fd, 0.3); os.write(fd, b'03'); more = drain(fd, 0.8); scr.feed(more.decode('latin1'))
        if 'BACKGROUND 01' not in scr.text() or 'Rooftop' not in scr.row(0):
            failures.append(f"r 03 in the background view did not show Rooftop's background (header {scr.row(0).rstrip()[:60]!r})")
        os.write(fd, b'\x1b'); more = drain(fd, 0.6); scr.feed(more.decode('latin1'))
        if 'BACKGROUND 01' in scr.row(23):
            failures.append("Esc did not leave the background view")
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
    rooms_after = open(os.path.join(sandbox, 'ROOMS.SPL'), 'rb').read()
    if rooms_after != rooms_before:
        failures.append("the key sequence (nothing in it edits) rewrote ROOMS.SPL differently")
    shutil.rmtree(sandbox, ignore_errors=True)
    for f in failures:
        print("FAIL:", f)
    print("tui: 30-row terminal - chunk panel drawn and scrolled, help clipped, background view, quit clean" if not failures else f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
