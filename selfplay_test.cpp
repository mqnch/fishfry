#include "search.h"
#include <cstdio>

// Recompute all derived state from the mailbox and compare to the incrementally
// maintained bitboards + zobrist key. Any mismatch means make/unmake corrupted state.
static bool consistent(const Position& p) {
    Bitboard byC[2] = {0,0}, byT[6] = {0,0,0,0,0,0};
    uint64_t key = 0;
    int kings[2] = {0,0};
    for (int s = 0; s < 64; ++s) {
        Piece pc = p.piece_on(s);
        if (pc == NO_PIECE) continue;
        byC[color_of(pc)] |= sq_bb(s);
        byT[ptype_of(pc)] |= sq_bb(s);
        key ^= Zob.psq[pc][s];
        if (ptype_of(pc) == KING) kings[color_of(pc)]++;
    }
    if (p.side == BLACK) key ^= Zob.side;
    key ^= Zob.castle[p.castling];
    if (p.epSquare != SQ_NONE) key ^= Zob.ep[file_of(p.epSquare)];

    bool ok = true;
    for (int c = 0; c < 2; ++c) if (p.byColor[c] != byC[c]) { printf("byColor[%d] mismatch\n", c); ok = false; }
    for (int t = 0; t < 6; ++t) if (p.byType[t] != byT[t])  { printf("byType[%d] mismatch\n", t); ok = false; }
    if (kings[0] != 1 || kings[1] != 1) { printf("king count %d/%d\n", kings[0], kings[1]); ok = false; }
    if (p.key != key) { printf("zobrist mismatch: have %016llx want %016llx\n",
                               (unsigned long long)p.key, (unsigned long long)key); ok = false; }
    return ok;
}

static int count_legal(Position& p) {
    MoveList ml; p.gen_pseudo(ml);
    int n = 0;
    for (int i = 0; i < ml.count; ++i) if (p.is_legal(ml.moves[i].move)) n++;
    return n;
}

static bool is_legal_now(Position& p, Move m) {
    MoveList ml; p.gen_pseudo(ml);
    for (int i = 0; i < ml.count; ++i)
        if (ml.moves[i].move == m && p.is_legal(m)) return true;
    return false;
}

int main(int argc, char** argv) {
    init_bitboards(); init_zobrist(); init_castle_mask(); init_eval(); init_search_tables();

    int depth = (argc > 1) ? atoi(argv[1]) : 7;
    const char* openings[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 1",
        "rnbqkb1r/pp1p1ppp/4pn2/2p5/2P5/2N2N2/PP1PPPPP/R1BQKB1R w KQkq - 0 1",
        "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 0 1",
        "rnbqkbnr/pp2pppp/2p5/3p4/3P4/2N5/PPP1PPPP/R1BQKBNR w KQkq - 0 1",
        "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 0 1",
    };
    Searcher* S = new Searcher();
    S->tt.resize(32);
    S->silent = true;

    int games = 0, whiteWins = 0, blackWins = 0, draws = 0;
    uint64_t totalPlies = 0;
    bool ok = true;

    for (auto opening : openings) {
        for (int rep = 0; rep < 2 && ok; ++rep) {   // two games per opening (TT reuse differs)
            games++;
            S->clear();
            S->pos.set(opening);
            if (!consistent(S->pos)) { printf("inconsistent after setup: %s\n", opening); ok = false; break; }

            int result = 0; // 0 draw/unfinished, 1 white mate, -1 black mate
            int ply;
            for (ply = 0; ply < 240; ++ply) {
                if (count_legal(S->pos) == 0) {
                    if (S->pos.in_check()) result = (S->pos.to_move() == WHITE) ? -1 : 1; // side to move is mated
                    break;
                }
                if (S->pos.rule50 >= 100 || S->pos.is_material_draw() || S->pos.is_repetition()) break;

                S->limits = Limits{}; S->limits.depth = depth;
                S->stop.store(false, std::memory_order_relaxed);
                S->think();
                Move m = S->bestMove;

                if (m == MOVE_NONE || !is_legal_now(S->pos, m)) {
                    printf("*** ILLEGAL/NULL bestmove %s in %s\n", move_to_uci(m).c_str(), S->pos.fen().c_str());
                    ok = false; break;
                }
                S->pos.do_move(m);
                if (!consistent(S->pos)) {
                    printf("*** inconsistent after %s\n", move_to_uci(m).c_str());
                    ok = false; break;
                }
            }
            totalPlies += ply;
            if (result == 1) whiteWins++; else if (result == -1) blackWins++; else draws++;
            printf("game %2d (%-2s d%d): %3d plies  result=%s\n", games, rep ? "r2":"r1", depth, ply,
                   result == 1 ? "1-0" : result == -1 ? "0-1" : "1/2");
            fflush(stdout);
        }
        if (!ok) break;
    }

    printf("\n%d games, %llu plies, W:%d B:%d D:%d\n", games, (unsigned long long)totalPlies,
           whiteWins, blackWins, draws);
    printf("%s\n", ok ? "SELF-PLAY OK: all moves legal, board state consistent throughout"
                      : "*** SELF-PLAY FAILED ***");
    delete S;
    return ok ? 0 : 1;
}
