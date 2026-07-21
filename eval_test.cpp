#include "eval.h"
#include <cstdio>
#include <sstream>
#include <vector>
#include <algorithm>

// Vertically mirror a FEN (swap colors + flip ranks). Eval must be invariant
// from the side-to-move's perspective.
static std::string mirror_fen(const std::string& fen) {
    std::istringstream ss(fen);
    std::string board, stm, cr, ep; ss >> board >> stm >> cr >> ep;
    std::vector<std::string> rows; std::string cur;
    for (char c : board) { if (c == '/') { rows.push_back(cur); cur.clear(); } else cur += c; }
    rows.push_back(cur);
    std::reverse(rows.begin(), rows.end());
    std::string nb;
    for (size_t i = 0; i < rows.size(); ++i) {
        for (char c : rows[i]) {
            if (c >= 'A' && c <= 'Z') nb += char(c - 'A' + 'a');
            else if (c >= 'a' && c <= 'z') nb += char(c - 'a' + 'A');
            else nb += c;
        }
        if (i + 1 < rows.size()) nb += '/';
    }
    std::string nstm = (stm == "w") ? "b" : "w";
    std::string ncr;
    for (char c : cr) {
        if (c == 'K') ncr += 'k'; else if (c == 'Q') ncr += 'q';
        else if (c == 'k') ncr += 'K'; else if (c == 'q') ncr += 'Q'; else ncr += c;
    }
    std::string nep = ep;
    if (ep != "-") nep = std::string() + ep[0] + char('1' + (7 - (ep[1] - '1')));
    return nb + " " + nstm + " " + ncr + " " + nep + " 0 1";
}

int main() {
    init_bitboards(); init_zobrist(); init_castle_mask(); init_eval();
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "8/5k2/8/8/8/3K4/4P3/8 w - - 0 1",
        "r2q1rk1/pp2ppbp/2np1np1/2p3B1/2B1P3/2NP1N2/PPP2PPP/R2Q1RK1 b - - 0 1",
    };
    bool ok = true;
    for (auto f : fens) {
        Position p; p.set(f);
        Position pm; pm.set(mirror_fen(f));
        int e1 = evaluate(p), e2 = evaluate(pm);
        bool match = (e1 == e2);
        ok &= match;
        printf("eval=%-6d mirror=%-6d %s   %s\n", e1, e2, match ? "OK" : "*** MISMATCH ***", f);
    }
    printf("\n%s\n", ok ? "EVAL SYMMETRY OK" : "EVAL SYMMETRY BROKEN");
    return ok ? 0 : 1;
}
