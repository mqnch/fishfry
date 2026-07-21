#!/usr/bin/env python3
"""Barebones interactive interface for playing Twin, BitterFish, or a human,
against each other in any combination, with an optional live Stockfish eval.

Assign each side (White/Black) a role — twin, bitterfish, or human — at
startup. Two engine roles auto-play each other; a human role prompts you for
moves (either to relay between a manual opponent and an engine, or just to
play yourself).
"""
import os
import queue
import shutil
import subprocess
import sys
import threading
import time

try:
    import chess
    import chess.pgn
    HAVE_CHESS_LIB = True
except ImportError:
    HAVE_CHESS_LIB = False

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TWIN_PATH = os.path.join(SCRIPT_DIR, "twin")
BITTERFISH_DIR = os.path.join(SCRIPT_DIR, "Bitterfish")
BITTERFISH_OUT = os.path.join(BITTERFISH_DIR, "out")

ROLES = ("twin", "bitterfish", "human")

LOG_LOCK = threading.Lock()


def open_log_file():
    log_dir = os.path.join(SCRIPT_DIR, "logs")
    os.makedirs(log_dir, exist_ok=True)
    path = os.path.join(log_dir, time.strftime("play_%Y%m%d_%H%M%S.log"))
    return open(path, "a", buffering=1), path


class Engine:
    """Generic UCI engine subprocess wrapper (Twin, BitterFish, or Stockfish)."""

    def __init__(self, cmd, cwd=None, name="engine", log=None):
        self.name = name
        self.log = log
        self.p = subprocess.Popen(
            cmd,
            cwd=cwd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,  # Twin's "d" command writes board+FEN to stderr
            text=True,
            bufsize=1,
        )
        self.q = queue.Queue()
        threading.Thread(target=self._reader, daemon=True).start()

    def _log_line(self, direction, line):
        if self.log is None:
            return
        with LOG_LOCK:
            self.log.write(f"{time.strftime('%H:%M:%S')} [{self.name}] {direction} {line}\n")

    def _reader(self):
        for line in self.p.stdout:
            line = line.rstrip("\n")
            self.q.put(line)
            self._log_line("<<<", line)

    def send(self, s):
        self._log_line(">>>", s)
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()

    def read_until(self, prefix, timeout=120):
        lines = []
        while True:
            line = self.q.get(timeout=timeout)
            lines.append(line)
            if line.startswith(prefix):
                return lines

    def handshake(self):
        self.send("uci")
        self.read_until("uciok")
        self.send("isready")
        self.read_until("readyok")
        self.send("ucinewgame")

    def quit(self):
        try:
            self.send("quit")
        except Exception:
            pass
        try:
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


def brew_prefix(formula):
    try:
        out = subprocess.run(["brew", "--prefix", formula], capture_output=True, text=True, timeout=10)
        return out.stdout.strip() if out.returncode == 0 else None
    except Exception:
        return None


def find_java():
    # Prefer the brew-installed JDK: /usr/bin/java on macOS is often just an
    # install-prompt stub with no actual runtime behind it.
    prefix = brew_prefix("openjdk")
    if prefix:
        candidate = os.path.join(prefix, "bin", "java")
        if os.path.isfile(candidate):
            return candidate
    return shutil.which("java")


def ask_role(side):
    while True:
        ans = input(f"Who plays {side}? [twin/bitterfish/human]: ").strip().lower()
        if ans in ROLES:
            return ans
        print("Please answer 'twin', 'bitterfish', or 'human'.")


def ask_seconds_per_move():
    while True:
        ans = input("Seconds per engine move? ").strip()
        try:
            secs = float(ans)
            if secs > 0:
                return secs
        except ValueError:
            pass
        print("Please enter a positive number.")


def ask_yes_no(prompt, default=True):
    suffix = "[Y/n]" if default else "[y/N]"
    ans = input(f"{prompt} {suffix}: ").strip().lower()
    if not ans:
        return default
    return ans in ("y", "yes")


def position_cmd(moves):
    return "position startpos moves " + " ".join(moves) if moves else "position startpos"


def print_board(lines):
    for line in lines:
        if line.startswith("FEN: "):
            print(line)
        elif line.strip():
            print(line)


def show_position(referee, moves):
    referee.send(position_cmd(moves))
    referee.send("d")
    lines = referee.read_until("FEN: ")
    print()
    print_board(lines)
    print()


def bestmove_of(lines):
    for line in lines:
        if line.startswith("bestmove"):
            parts = line.split()
            return parts[1] if len(parts) > 1 else None
    return None


def side_to_move_of(lines):
    for line in lines:
        if line.startswith("FEN: "):
            fields = line[len("FEN: "):].split()
            if len(fields) >= 2:
                return fields[1]
    return None


def is_game_over(referee, moves):
    referee.send(position_cmd(moves))
    referee.send("go depth 1")
    lines = referee.read_until("bestmove")
    bm = bestmove_of(lines)
    return bm is None or bm == "0000"


def is_legal_move(referee, moves, candidate):
    referee.send("position startpos moves " + " ".join(moves + [candidate]))
    referee.send("d")
    lines = referee.read_until("FEN: ")
    new_side = side_to_move_of(lines)
    expected = "b" if len(moves) % 2 == 0 else "w"  # side to move before candidate, flipped
    return new_side == expected


def last_score_of(lines):
    score = None
    for line in lines:
        if line.startswith("info") and " score " in line:
            tok = line.split()
            i = tok.index("score")
            score = (tok[i + 1], int(tok[i + 2]))  # ("cp", X) or ("mate", Y)
    return score


def format_eval(score, side_to_move):
    if score is None:
        return None
    kind, val = score
    if side_to_move == "b":  # UCI score is from the mover's perspective; flip to White's
        val = -val
    if kind == "cp":
        return f"Eval (White's perspective): {val / 100:+.2f}"
    return f"Eval (White's perspective): #{val:+d} (mate)"


def show_eval(stockfish, moves, side_to_move):
    if stockfish is None:
        return
    stockfish.send(position_cmd(moves))
    stockfish.send("go movetime 200")
    lines = stockfish.read_until("bestmove")
    text = format_eval(last_score_of(lines), side_to_move)
    if text:
        print(text)


def make_twin_engine(name="twin", log=None):
    if not os.path.isfile(TWIN_PATH):
        print(f"Twin binary not found at {TWIN_PATH}. Run 'make' first.", file=sys.stderr)
        sys.exit(1)
    e = Engine([TWIN_PATH], name=name, log=log)
    e.handshake()
    return e


def make_bitterfish_engine(name="bitterfish", log=None):
    java = find_java()
    if java is None:
        print("Java not found. Install a JDK, e.g. 'brew install openjdk'.", file=sys.stderr)
        sys.exit(1)
    if not os.path.isdir(BITTERFISH_OUT):
        print(f"BitterFish isn't compiled yet (expected {BITTERFISH_OUT}).", file=sys.stderr)
        sys.exit(1)
    e = Engine([java, "-cp", BITTERFISH_OUT, "Main"], cwd=BITTERFISH_DIR, name=name, log=log)
    e.handshake()
    return e


def make_stockfish_engine(name="stockfish", log=None):
    path = shutil.which("stockfish")
    if path is None:
        print("Stockfish not found on PATH — eval display disabled. "
              "Install via 'brew install stockfish' to enable it.")
        return None
    e = Engine([path], name=name, log=log)
    e.handshake()
    return e


def format_game(moves, roles, resigned_side=None):
    """PGN (paste into chess.com's analysis board) if python-chess is
    available, otherwise a plain UCI move list as a fallback."""
    if not HAVE_CHESS_LIB:
        return "python-chess not installed (pip install chess) — raw UCI moves:\n" + " ".join(moves)
    try:
        board = chess.Board()
        for uci in moves:
            board.push_uci(uci)
        game = chess.pgn.Game.from_board(board)
        game.headers["Event"] = "play.py engine match"
        game.headers["White"] = roles["w"].capitalize()
        game.headers["Black"] = roles["b"].capitalize()
        outcome = board.outcome()
        if resigned_side is not None:
            game.headers["Result"] = "0-1" if resigned_side == "w" else "1-0"
        elif outcome is not None:
            game.headers["Result"] = outcome.result()
        else:
            game.headers["Result"] = "*"
        return str(game)
    except Exception as e:
        return f"(couldn't build PGN: {e})\nRaw UCI moves:\n" + " ".join(moves)


def make_player_engine(role, name, log=None):
    if role == "twin":
        return make_twin_engine(name=name, log=log)
    if role == "bitterfish":
        return make_bitterfish_engine(name=name, log=log)
    return None  # human


def main():
    white_role = ask_role("White")
    black_role = ask_role("Black")
    seconds_per_move = ask_seconds_per_move()
    movetime_ms = int(seconds_per_move * 1000)
    want_eval = ask_yes_no("Show Stockfish eval each move?", default=True)

    log_fh, log_path = open_log_file()
    print(f"Logging UCI traffic to {log_path}")

    roles = {"w": white_role, "b": black_role}
    referee = make_twin_engine(name="referee", log=log_fh)  # board display + human-move legality
    engines = {"w": None, "b": None}
    stockfish = make_stockfish_engine(log=log_fh) if want_eval else None
    moves = []
    resigned_side = None

    try:
        for side in ("w", "b"):
            engines[side] = make_player_engine(roles[side], name=f"{side}:{roles[side]}", log=log_fh)

        side_to_move = "w"

        while True:
            show_position(referee, moves)

            role = roles[side_to_move]
            side_name = "White" if side_to_move == "w" else "Black"

            if is_game_over(referee, moves):
                print(f"Game over: {side_name} has no legal move (checkmate or stalemate). See PGN below.")
                break

            show_eval(stockfish, moves, side_to_move)

            if role == "human":
                ans = input(f"Enter {side_name}'s move (UCI, e.g. e2e4), or 'quit'/'resign': ").strip()
                if ans.lower() in ("quit", "resign"):
                    if ans.lower() == "resign":
                        resigned_side = side_to_move
                    print("Exiting.")
                    break
                if not is_legal_move(referee, moves, ans):
                    print("Illegal or malformed move, try again.")
                    continue
                moves.append(ans)
            else:
                engine = engines[side_to_move]
                engine.send(position_cmd(moves))
                engine.send(f"go movetime {movetime_ms}")
                lines = engine.read_until("bestmove")
                bm = bestmove_of(lines)
                if bm is None or bm == "0000":
                    print(f"Game over: {side_name} has no legal move (checkmate or stalemate). See PGN below.")
                    break
                other_role = roles["b" if side_to_move == "w" else "w"]
                note = "  (relay this to your friend's agent)" if other_role == "human" else ""
                print(f">>> {role.capitalize()} ({side_name}) plays: {bm}{note}")
                moves.append(bm)

            side_to_move = "b" if side_to_move == "w" else "w"
    except KeyboardInterrupt:
        print("\nInterrupted, exiting.")
    finally:
        referee.quit()
        for e in engines.values():
            if e is not None:
                e.quit()
        if stockfish is not None:
            stockfish.quit()
        log_fh.close()

    if moves:
        print()
        print("=" * 40)
        print("Game record — paste into chess.com's analysis board:")
        print()
        print(format_game(moves, roles, resigned_side))
    else:
        print("No moves were played.")
    print(f"\nFull UCI traffic log: {log_path}")


if __name__ == "__main__":
    main()
