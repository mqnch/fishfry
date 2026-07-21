// ============================================================================
//  Twin — a UCI chess engine.  Single translation unit.
//  Build:  clang++ -std=c++17 -O3 -o twin main.cpp
// ============================================================================
#include "search.h"
#include <iostream>
#include <sstream>
#include <thread>

static const char* START_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static Searcher searcher;
static std::thread searchThread;

static void stop_search() {
    searcher.stop.store(true, std::memory_order_relaxed);
    if (searchThread.joinable()) searchThread.join();
}

// Apply a UCI move string by matching it against the legal move list.
static bool apply_uci_move(const std::string& tok) {
    MoveList ml; searcher.pos.gen_pseudo(ml);
    for (int i = 0; i < ml.count; ++i) {
        Move m = ml.moves[i].move;
        if (searcher.pos.is_legal(m) && move_to_uci(m) == tok) { searcher.pos.do_move(m); return true; }
    }
    return false;
}

static void cmd_position(std::istringstream& is) {
    std::string token;
    is >> token;
    std::string fen;
    if (token == "startpos") {
        fen = START_FEN;
        is >> token; // maybe "moves"
    } else if (token == "fen") {
        fen.clear();
        while (is >> token && token != "moves") fen += token + " ";
    } else return;

    searcher.pos.set(fen);

    if (token == "moves")
        while (is >> token) if (!apply_uci_move(token)) break;
}

static void cmd_go(std::istringstream& is) {
    Limits lim;
    std::string token;
    bool anyLimit = false;
    while (is >> token) {
        if      (token == "wtime")     { is >> lim.time[WHITE]; lim.timed = true; anyLimit = true; }
        else if (token == "btime")     { is >> lim.time[BLACK]; lim.timed = true; anyLimit = true; }
        else if (token == "winc")      { is >> lim.inc[WHITE]; }
        else if (token == "binc")      { is >> lim.inc[BLACK]; }
        else if (token == "movestogo") { is >> lim.movestogo; }
        else if (token == "movetime")  { is >> lim.movetime; lim.timed = true; anyLimit = true; }
        else if (token == "depth")     { is >> lim.depth; anyLimit = true; }
        else if (token == "nodes")     { is >> lim.nodes; anyLimit = true; }
        else if (token == "infinite")  { lim.infinite = true; anyLimit = true; }
        else if (token == "ponder")    { /* ignored */ }
        else if (token == "mate")      { int n; is >> n; lim.depth = n * 2; anyLimit = true; }
    }
    if (!anyLimit) lim.infinite = true;   // bare "go" -> search until "stop"

    searcher.limits = lim;
    searcher.stop.store(false, std::memory_order_relaxed);
    searchThread = std::thread([]{ searcher.think(); });
}

// Debug helpers
static uint64_t perft(Position& p, int depth) {
    if (depth == 0) return 1;
    MoveList ml; p.gen_pseudo(ml);
    uint64_t n = 0;
    for (int i = 0; i < ml.count; ++i) {
        Move m = ml.moves[i].move;
        if (!p.is_legal(m)) continue;
        if (depth == 1) { n++; continue; }
        p.do_move(m); n += perft(p, depth - 1); p.undo_move(m);
    }
    return n;
}

static void cmd_bench() {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r1bq1rk1/pp2bppp/2n2n2/2pp4/3P4/2N1PN2/PP2BPPP/R1BQ1RK1 w - - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r2q1rk1/1pp1bppp/p1np1n2/4p1B1/2B1P1b1/2NP1N2/PPPQ1PPP/R3K2R w KQ - 0 1",
    };
    uint64_t total = 0;
    int64_t t0 = now_ms();
    for (auto f : fens) {
        searcher.clear();
        searcher.pos.set(f);
        searcher.limits = Limits{}; searcher.limits.depth = 12;
        searcher.stop.store(false, std::memory_order_relaxed);
        searcher.think();
        total += searcher.nodes;
    }
    int64_t dt = now_ms() - t0;
    printf("bench: %llu nodes %llu nps\n", (unsigned long long)total,
           (unsigned long long)(dt > 0 ? total * 1000 / dt : total));
    fflush(stdout);
}

int main() {
    init_bitboards();
    init_zobrist();
    init_castle_mask();
    init_eval();
    init_search_tables();
    searcher.tt.resize(64);
    searcher.pos.set(START_FEN);

    std::cout.setf(std::ios::unitbuf); // auto-flush cout
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string cmd;
        is >> cmd;

        if (cmd == "uci") {
            printf("id name Twin 1.0\n");
            printf("id author fpan\n");
            printf("option name Hash type spin default 64 min 1 max 4096\n");
            printf("option name Threads type spin default 1 min 1 max 1\n");
            printf("option name Ponder type check default false\n");
            printf("uciok\n");
            fflush(stdout);
        }
        else if (cmd == "isready") { printf("readyok\n"); fflush(stdout); }
        else if (cmd == "setoption") {
            std::string t, name, value; is >> t; // "name"
            std::string nm;
            while (is >> t && t != "value") { if (!nm.empty()) nm += " "; nm += t; }
            std::getline(is, value);
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);
            if (nm == "Hash") { stop_search(); int mb = std::max(1, atoi(value.c_str())); searcher.tt.resize(mb); }
            // Threads / Ponder ignored
        }
        else if (cmd == "ucinewgame") { stop_search(); searcher.clear(); searcher.pos.set(START_FEN); }
        else if (cmd == "position")   { stop_search(); cmd_position(is); }
        else if (cmd == "go")         { stop_search(); cmd_go(is); }
        else if (cmd == "stop")       { stop_search(); }
        else if (cmd == "ponderhit")  { /* we don't ponder; ignore */ }
        else if (cmd == "quit")       { stop_search(); break; }
        // ---- debug / convenience ----
        else if (cmd == "d")          { searcher.pos.print(); }
        else if (cmd == "eval")       { printf("eval: %d cp\n", evaluate(searcher.pos)); fflush(stdout); }
        else if (cmd == "perft")      { int d = 5; is >> d; int64_t t0 = now_ms();
                                        uint64_t n = perft(searcher.pos, d); int64_t dt = now_ms() - t0;
                                        printf("perft(%d) = %llu  (%lld ms)\n", d, (unsigned long long)n, (long long)dt); fflush(stdout); }
        else if (cmd == "bench")      { cmd_bench(); }
    }
    stop_search();
    return 0;
}
