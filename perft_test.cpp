#include "position.h"
#include <cstdio>
#include <chrono>

static uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    MoveList ml; pos.gen_pseudo(ml);
    uint64_t nodes = 0;
    for (int i = 0; i < ml.count; ++i) {
        Move m = ml.moves[i].move;
        if (!pos.is_legal(m)) continue;
        if (depth == 1) { nodes += 1; continue; }
        pos.do_move(m);
        nodes += perft(pos, depth - 1);
        pos.undo_move(m);
    }
    return nodes;
}

static void divide(Position& pos, int depth) {
    MoveList ml; pos.gen_pseudo(ml);
    uint64_t total = 0;
    for (int i = 0; i < ml.count; ++i) {
        Move m = ml.moves[i].move;
        if (!pos.is_legal(m)) continue;
        pos.do_move(m);
        uint64_t n = perft(pos, depth - 1);
        pos.undo_move(m);
        total += n;
        printf("%s: %llu\n", move_to_uci(m).c_str(), (unsigned long long)n);
    }
    printf("total: %llu\n", (unsigned long long)total);
}

struct Case { const char* fen; int depth; uint64_t expected; const char* name; };

int main(int argc, char** argv) {
    init_bitboards();
    init_zobrist();
    init_castle_mask();

    if (argc >= 3) { // custom divide:  perft_test "<fen>" <depth>
        Position p; p.set(argv[1]);
        divide(p, atoi(argv[2]));
        return 0;
    }

    Case cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6, 119060324, "startpos d6"},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5, 193690690, "kiwipete d5"},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11030083, "position3 d6"},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 5, 15833292, "position4 d5"},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5, 89941194, "position5 d5"},
        {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 5, 164075551, "position6 d5"},
    };

    bool allok = true;
    for (auto& c : cases) {
        Position p; p.set(c.fen);
        auto t0 = std::chrono::steady_clock::now();
        uint64_t n = perft(p, c.depth);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        bool ok = (n == c.expected);
        allok &= ok;
        printf("%-16s depth %d: %13llu  expected %13llu  %s  (%.0f ms, %.1f Mnps)\n",
               c.name, c.depth, (unsigned long long)n, (unsigned long long)c.expected,
               ok ? "OK" : "*** FAIL ***", ms, n / (ms * 1000.0));
        fflush(stdout);
    }
    printf("\n%s\n", allok ? "ALL PERFT TESTS PASSED" : "PERFT FAILURES PRESENT");
    return allok ? 0 : 1;
}
