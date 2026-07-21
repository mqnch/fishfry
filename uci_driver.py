#!/usr/bin/env python3
import subprocess, sys, time, threading, queue

ENGINE = sys.argv[1]

class Engine:
    def __init__(self, path):
        self.p = subprocess.Popen(path, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   text=True, bufsize=1)
        self.q = queue.Queue()
        threading.Thread(target=self._reader, daemon=True).start()
    def _reader(self):
        for line in self.p.stdout:
            self.q.put(line.rstrip("\n"))
    def send(self, s):
        self.p.stdin.write(s + "\n"); self.p.stdin.flush()
    def read_until(self, token, timeout=15):
        lines = []
        end = time.time() + timeout
        while time.time() < end:
            try:
                line = self.q.get(timeout=end - time.time())
            except queue.Empty:
                break
            lines.append(line)
            if line.split(" ")[0] == token or line.startswith(token):
                return lines
        raise TimeoutError(f"timed out waiting for '{token}'; got:\n" + "\n".join(lines))
    def bestmove_of(self, lines):
        for line in lines:
            if line.startswith("bestmove"):
                return line.split()[1]
        return None
    def last_score(self, lines):
        sc = None
        for line in lines:
            if line.startswith("info") and " score " in line:
                t = line.split()
                i = t.index("score")
                sc = t[i+1] + " " + t[i+2]
        return sc

fails = 0
def check(name, cond, detail=""):
    global fails
    print(f"[{'PASS' if cond else 'FAIL'}] {name}" + (f"  ({detail})" if detail else ""))
    if not cond: fails += 1

e = Engine(ENGINE)

# 1. uci handshake
e.send("uci")
lines = e.read_until("uciok")
check("uci -> uciok", any(l == "uciok" for l in lines))
check("id name present", any(l.startswith("id name") for l in lines))

# 2. isready
e.send("isready")
check("isready -> readyok", any(l == "readyok" for l in e.read_until("readyok")))

e.send("ucinewgame")
e.send("isready"); e.read_until("readyok")

# 3. Mate in 1: Ra1-a8#
e.send("position fen 6k1/5ppp/8/8/8/8/8/R6K w - - 0 1")
e.send("go depth 10")
lines = e.read_until("bestmove")
bm = e.bestmove_of(lines); sc = e.last_score(lines)
check("mate-in-1 finds a1a8", bm == "a1a8", f"bestmove={bm}")
check("mate-in-1 reports mate score", sc is not None and sc.startswith("mate"), f"score={sc}")

# 4. Simple tactic: king captures a checking, undefended rook (Kxe2)
e.send("position fen 4k3/8/8/8/8/8/4r3/4K2R w K - 0 1")
e.send("go depth 12")
lines = e.read_until("bestmove")
bm = e.bestmove_of(lines)
check("tactic captures free rook (e1e2)", bm == "e1e2", f"bestmove={bm}")

# 5. Normal position returns a legal-looking move quickly by depth
e.send("position startpos moves e2e4 e7e5 g1f3")
e.send("go depth 14")
t0 = time.time()
lines = e.read_until("bestmove", timeout=30)
dt = time.time() - t0
bm = e.bestmove_of(lines)
check("startpos line returns a move", bm is not None and len(bm) >= 4, f"bestmove={bm}, {dt:.2f}s")

# 6. Time management: must respect a small time budget and not hang
e.send("position startpos")
e.send("go wtime 1000 btime 1000 winc 0 binc 0")
t0 = time.time()
lines = e.read_until("bestmove", timeout=10)
dt = time.time() - t0
bm = e.bestmove_of(lines)
check("time control returns a move", bm is not None, f"bestmove={bm}")
check("time control within budget", dt < 2.0, f"took {dt:.2f}s")

# 7. movetime
e.send("position startpos")
e.send("go movetime 500")
t0 = time.time()
lines = e.read_until("bestmove", timeout=10)
dt = time.time() - t0
check("movetime 500 ~respected", 0.3 < dt < 1.5, f"took {dt:.2f}s")

# 8. stop during infinite search
e.send("position startpos")
e.send("go infinite")
time.sleep(0.6)
e.send("stop")
t0 = time.time()
lines = e.read_until("bestmove", timeout=5)
dt = time.time() - t0
check("go infinite + stop returns a move", e.bestmove_of(lines) is not None, f"{dt:.2f}s")

# 9. Sequential search after stop still works
e.send("isready"); e.read_until("readyok")
e.send("position startpos moves d2d4 d7d5 c2c4")
e.send("go depth 10")
bm = e.bestmove_of(e.read_until("bestmove"))
check("engine responsive after stop", bm is not None, f"bestmove={bm}")

e.send("quit")
print()
print("ALL UCI TESTS PASSED" if fails == 0 else f"{fails} UCI TEST(S) FAILED")
sys.exit(1 if fails else 0)
