#pragma once
// ============================================================================
//  Search: transposition table, iterative deepening, principal variation
//  search (PVS), null-move pruning, LMR, killers/history/countermoves,
//  quiescence search with SEE pruning, time management.
// ============================================================================
#include "eval.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdio>

constexpr int MAX_PLY = 128;
constexpr int VALUE_INF        = 32001;
constexpr int VALUE_MATE       = 32000;
constexpr int VALUE_MATE_IN_MAX= VALUE_MATE - MAX_PLY; // scores >= this are mates
constexpr int VALUE_NONE       = 32002;

inline int mate_in(int ply)  { return  VALUE_MATE - ply; }
inline int mated_in(int ply) { return -VALUE_MATE + ply; }

inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
//  Transposition table
// ---------------------------------------------------------------------------
enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    uint16_t key16;
    uint16_t move;
    int16_t  value;
    int16_t  eval;
    uint8_t  depth;
    uint8_t  genBound; // bits 0-1 bound, bits 2-7 generation
};
static_assert(sizeof(TTEntry) == 10, "TTEntry size");

class TranspositionTable {
public:
    TTEntry* table = nullptr;
    size_t   count = 0;   // power of two
    uint8_t  generation = 0;

    ~TranspositionTable() { free(table); }

    void resize(size_t mb) {
        free(table);
        size_t bytes = mb * 1024 * 1024;
        size_t n = 1;
        while (n * sizeof(TTEntry) * 2 <= bytes) n <<= 1;   // largest power of two that fits
        if (n < 1024) n = 1024;
        count = n;
        table = (TTEntry*)calloc(count, sizeof(TTEntry));
    }
    void clear() { if (table) memset(table, 0, count * sizeof(TTEntry)); generation = 0; }
    void new_generation() { generation = (generation + 1) & 0x3F; }

    inline TTEntry* probe(uint64_t key, bool& hit) const {
        TTEntry* e = &table[key & (count - 1)];
        uint16_t k16 = uint16_t(key >> 48);
        hit = (e->genBound != 0 && e->key16 == k16);
        return e;
    }
    inline void store(uint64_t key, Move move, int value, int eval, int depth, Bound bound) {
        TTEntry* e = &table[key & (count - 1)];
        uint16_t k16 = uint16_t(key >> 48);
        // Replacement: overwrite if empty, same position, exact bound, deeper, or older gen.
        if (e->key16 != k16 || bound == BOUND_EXACT ||
            depth + 4 + 2 * (bound == BOUND_EXACT) > e->depth ||
            (e->genBound >> 2) != generation) {
            if (move != MOVE_NONE || e->key16 != k16) e->move = uint16_t(move);
            e->key16 = k16;
            e->value = int16_t(value);
            e->eval  = int16_t(eval);
            e->depth = uint8_t(depth);
            e->genBound = uint8_t((generation << 2) | bound);
        }
    }
};

// ---------------------------------------------------------------------------
//  Search limits
// ---------------------------------------------------------------------------
struct Limits {
    int64_t time[2] = {0, 0};
    int64_t inc[2]  = {0, 0};
    int     movestogo = 0;
    int64_t movetime = 0;
    int     depth = 0;
    uint64_t nodes = 0;
    bool    infinite = false;
    bool    timed = false;   // any of time/movetime set
};

// LMR reductions, initialised once.
inline int LMRTable[64][64];
inline void init_search_tables() {
    for (int d = 1; d < 64; ++d)
        for (int m = 1; m < 64; ++m)
            LMRTable[d][m] = int(0.75 + std::log(double(d)) * std::log(double(m)) / 2.25);
}

// ---------------------------------------------------------------------------
//  Searcher
// ---------------------------------------------------------------------------
struct RootMove {
    Move move;
    int  score;
    int  prevScore;
    Move pv[MAX_PLY];
    int  pvLen;
};

class Searcher {
public:
    Position pos;
    TranspositionTable tt;
    std::atomic<bool> stop{false};
    Limits limits;

    uint64_t nodes = 0;
    int64_t  startTime = 0;
    int64_t  softLimit = 0, hardLimit = 0;
    bool     useTime = false;
    int      selDepth = 0;

    int  history[2][64][64];
    Move killers[MAX_PLY + 2][2];
    Move counter[2][64][64];

    Move pvTable[MAX_PLY + 2][MAX_PLY + 2];
    int  pvLen[MAX_PLY + 2];

    std::vector<RootMove> rootMoves;
    Move bestMove = MOVE_NONE;
    int  rootDepth = 0;
    bool silent = false;   // suppress UCI output (used by test harnesses)

    void clear() {
        memset(history, 0, sizeof(history));
        memset(killers, 0, sizeof(killers));
        memset(counter, 0, sizeof(counter));
        tt.clear();
    }

    // ---- time control ----
    void set_time_management() {
        Color us = pos.to_move();
        if (limits.movetime > 0) {
            softLimit = hardLimit = std::max<int64_t>(1, limits.movetime);
            useTime = true;
        } else if (limits.timed) {                   // some clock was given
            int64_t t = std::max<int64_t>(0, limits.time[us]);
            int64_t inc = std::max<int64_t>(0, limits.inc[us]);
            int64_t overhead = 25;
            int mtg = limits.movestogo > 0 ? limits.movestogo : 30;
            int64_t avail = std::max<int64_t>(1, t - overhead);
            int64_t base = avail / mtg + inc * 3 / 4;
            softLimit = std::max<int64_t>(1, base * 6 / 10);
            hardLimit = std::min<int64_t>(avail * 4 / 5, base * 5);
            hardLimit = std::max<int64_t>(hardLimit, softLimit);
            hardLimit = std::max<int64_t>(hardLimit, 1);
            useTime = true;
        } else {
            useTime = false; // depth / nodes / infinite
        }
    }

    inline bool time_up() {
        if (!useTime) return false;
        return (now_ms() - startTime) >= hardLimit;
    }
    inline void check_stop() {
        if ((nodes & 2047) == 0) {
            if (time_up()) stop.store(true, std::memory_order_relaxed);
            if (limits.nodes && nodes >= limits.nodes) stop.store(true, std::memory_order_relaxed);
        }
    }

    // ---- move ordering ----
    inline bool is_capture(Move m) const {
        MoveType t = type_of(m);
        return t == MT_ENPASSANT || (t != MT_CASTLE && pos.piece_on(to_sq(m)) != NO_PIECE);
    }
    inline int mvv_lva(Move m) const {
        int victim = (type_of(m) == MT_ENPASSANT) ? PAWN : ptype_of(pos.piece_on(to_sq(m)));
        int attacker = ptype_of(pos.piece_on(from_sq(m)));
        return SeeValue[victim] * 16 - attacker;
    }
    void score_moves(MoveList& ml, Move ttMove, int ply, Move prevMove) {
        Color us = pos.to_move();
        int pf = from_sq(prevMove), pt = to_sq(prevMove);
        Move cm = (prevMove != MOVE_NONE && prevMove != MOVE_NULL) ? counter[us][pf][pt] : MOVE_NONE;
        for (int i = 0; i < ml.count; ++i) {
            Move m = ml.moves[i].move;
            int s;
            if (m == ttMove) s = 2000000000;
            else if (type_of(m) == MT_PROMO && promo_of(m) == QUEEN) s = 1500000 + mvv_lva(m);
            else if (is_capture(m)) {
                int base = mvv_lva(m);
                s = pos.see_ge(m, 0) ? 1000000 + base : -1000000 + base;
            } else if (m == killers[ply][0]) s = 900000;
            else if (m == killers[ply][1]) s = 800000;
            else if (m == cm) s = 700000;
            else s = history[us][from_sq(m)][to_sq(m)];
            ml.moves[i].score = s;
        }
    }
    inline Move pick_best(MoveList& ml, int i) {
        int best = i;
        for (int j = i + 1; j < ml.count; ++j)
            if (ml.moves[j].score > ml.moves[best].score) best = j;
        std::swap(ml.moves[i], ml.moves[best]);
        return ml.moves[i].move;
    }

    void update_history(int ply, Move best, Move* quiets, int nq, int depth, Color us, Move prevMove) {
        int bonus = std::min(depth * depth, 1200);
        auto add = [&](Move m, int b) {
            int& h = history[us][from_sq(m)][to_sq(m)];
            h += b - h * std::abs(b) / 16384;
        };
        add(best, bonus);
        for (int i = 0; i < nq; ++i) if (quiets[i] != best) add(quiets[i], -bonus);
        if (killers[ply][0] != best) { killers[ply][1] = killers[ply][0]; killers[ply][0] = best; }
        if (prevMove != MOVE_NONE && prevMove != MOVE_NULL)
            counter[us][from_sq(prevMove)][to_sq(prevMove)] = best;
    }

    bool has_non_pawn_material(Color c) const {
        return (pos.pieces(c) & ~pos.pieces(c, PAWN) & ~pos.pieces(c, KING)) != 0;
    }

    int qsearch(int alpha, int beta, int ply);
    int search(int depth, int alpha, int beta, int ply, bool pvNode, Move prevMove);
    int search_root(int alpha, int beta);
    void think();
    std::string pv_string();
};

// ---------------------------------------------------------------------------
//  Quiescence search
// ---------------------------------------------------------------------------
inline int Searcher::qsearch(int alpha, int beta, int ply) {
    nodes++;
    check_stop();
    if (stop.load(std::memory_order_relaxed)) return 0;
    if (ply >= MAX_PLY) return evaluate(pos);
    if (ply > selDepth) selDepth = ply;

    bool inCheck = pos.in_check();
    int best = -VALUE_INF;
    int stand = VALUE_NONE;

    if (!inCheck) {
        stand = evaluate(pos);
        best = stand;
        if (best >= beta) return best;
        if (best > alpha) alpha = best;
    }

    MoveList ml; pos.gen_pseudo(ml);
    score_moves(ml, MOVE_NONE, ply, MOVE_NONE);

    int legal = 0;
    for (int i = 0; i < ml.count; ++i) {
        Move m = pick_best(ml, i);
        bool cap = is_capture(m);
        bool promo = type_of(m) == MT_PROMO;
        if (!inCheck) {
            if (!cap && !promo) continue;                 // only tactical moves
            if (cap && !promo) {
                // delta pruning
                int victim = (type_of(m) == MT_ENPASSANT) ? PAWN : ptype_of(pos.piece_on(to_sq(m)));
                if (stand + SeeValue[victim] + 150 <= alpha) continue;
                if (!pos.see_ge(m, 0)) continue;          // skip losing captures
            }
        }
        if (!pos.is_legal(m)) continue;
        legal++;
        pos.do_move(m);
        int score = -qsearch(-beta, -alpha, ply + 1);
        pos.undo_move(m);
        if (stop.load(std::memory_order_relaxed)) return 0;
        if (score > best) {
            best = score;
            if (score > alpha) { alpha = score; if (alpha >= beta) break; }
        }
    }
    if (inCheck && legal == 0) return mated_in(ply);
    return best;
}

// ---------------------------------------------------------------------------
//  Main alpha-beta search (negamax, PVS)
// ---------------------------------------------------------------------------
inline int Searcher::search(int depth, int alpha, int beta, int ply, bool pvNode, Move prevMove) {
    pvLen[ply] = 0;
    if (depth <= 0) return qsearch(alpha, beta, ply);

    nodes++;
    check_stop();
    if (stop.load(std::memory_order_relaxed)) return 0;
    if (ply > selDepth) selDepth = ply;

    bool rootNode = (ply == 0);
    if (!rootNode) {
        if (pos.is_repetition() || pos.is_material_draw()) return 0;
        if (pos.rule50 >= 100) {
            // 50-move draw, unless it is checkmate (mate takes precedence)
            if (!pos.in_check()) return 0;
            MoveList em; pos.gen_pseudo(em);
            for (int i = 0; i < em.count; ++i) if (pos.is_legal(em.moves[i].move)) return 0;
            return mated_in(ply);
        }
        // Mate distance pruning
        alpha = std::max(alpha, mated_in(ply));
        beta  = std::min(beta,  mate_in(ply + 1));
        if (alpha >= beta) return alpha;
    }
    if (ply >= MAX_PLY) return evaluate(pos);

    // TT probe
    bool ttHit;
    TTEntry* tte = tt.probe(pos.key, ttHit);
    Move ttMove = ttHit ? Move(tte->move) : MOVE_NONE;
    int ttValue = ttHit ? int(tte->value) : VALUE_NONE;
    if (ttHit && ttValue != VALUE_NONE) {
        if (ttValue >= VALUE_MATE_IN_MAX) ttValue -= ply;
        else if (ttValue <= -VALUE_MATE_IN_MAX) ttValue += ply;
    }
    Bound ttBound = ttHit ? Bound(tte->genBound & 3) : BOUND_NONE;

    if (!pvNode && ttHit && tte->depth >= depth && ttValue != VALUE_NONE) {
        if (ttBound == BOUND_EXACT
         || (ttBound == BOUND_LOWER && ttValue >= beta)
         || (ttBound == BOUND_UPPER && ttValue <= alpha))
            return ttValue;
    }

    bool inCheck = pos.in_check();
    int eval;
    if (inCheck) eval = VALUE_NONE;
    else if (ttHit && tte->eval != int16_t(VALUE_NONE)) eval = tte->eval;
    else eval = evaluate(pos);

    // Reverse futility / static null-move pruning
    if (!pvNode && !inCheck && depth <= 8 && eval < VALUE_MATE_IN_MAX &&
        eval - 80 * depth >= beta)
        return eval;

    // Null-move pruning
    if (!pvNode && !inCheck && depth >= 3 && eval >= beta &&
        has_non_pawn_material(pos.to_move()) && prevMove != MOVE_NULL) {
        int R = 3 + depth / 4 + std::min((eval - beta) / 200, 3);
        pos.do_null();
        int score = -search(depth - 1 - R, -beta, -beta + 1, ply + 1, false, MOVE_NULL);
        pos.undo_null();
        if (stop.load(std::memory_order_relaxed)) return 0;
        if (score >= beta) {
            if (score >= VALUE_MATE_IN_MAX) score = beta;
            return score;
        }
    }

    // Razoring: very low static eval at shallow depth -> drop to qsearch
    if (!pvNode && !inCheck && depth <= 3 && eval + 200 * depth < alpha) {
        int q = qsearch(alpha, beta, ply);
        if (q < alpha) return q;
    }

    MoveList ml; pos.gen_pseudo(ml);
    score_moves(ml, ttMove, ply, prevMove);

    int best = -VALUE_INF;
    Move bestMoveLocal = MOVE_NONE;
    int legal = 0;
    Move quiets[64]; int nq = 0;
    int origAlpha = alpha;
    Color us = pos.to_move();

    for (int i = 0; i < ml.count; ++i) {
        Move m = pick_best(ml, i);
        bool cap = is_capture(m);
        bool promo = type_of(m) == MT_PROMO;
        bool quiet = !cap && !promo;

        // Late move / futility pruning for quiet moves at shallow depth
        if (!rootNode && !inCheck && quiet && best > -VALUE_MATE_IN_MAX) {
            if (depth <= 8 && legal >= 3 + depth * depth) continue;
            if (depth <= 6 && eval != VALUE_NONE && eval + 120 + 90 * depth <= alpha) continue;
            if (depth <= 8 && !pos.see_ge(m, -50 * depth)) continue;
        }
        // SEE pruning for bad captures at shallow depth
        if (!rootNode && !inCheck && cap && depth <= 6 && best > -VALUE_MATE_IN_MAX) {
            if (!pos.see_ge(m, -90 * depth)) continue;
        }

        if (!pos.is_legal(m)) continue;
        legal++;
        bool givesCheck = pos.gives_check(m);
        if (quiet && nq < 64) quiets[nq++] = m;

        int newDepth = depth - 1;
        int ext = (givesCheck) ? 1 : 0;      // check extension
        newDepth += ext;

        pos.do_move(m);

        int score;
        if (legal == 1) {
            score = -search(newDepth, -beta, -alpha, ply + 1, pvNode, m);
        } else {
            int R = 0;
            if (depth >= 3 && quiet && !givesCheck) {
                R = LMRTable[std::min(depth, 63)][std::min(legal, 63)];
                if (!pvNode) R += 1;
                if (m == killers[ply][0] || m == killers[ply][1]) R -= 1;
                R -= history[us][from_sq(m)][to_sq(m)] / 8192;
                if (R < 0) R = 0;
                if (R > newDepth) R = newDepth;
            }
            score = -search(newDepth - R, -alpha - 1, -alpha, ply + 1, false, m);
            if (score > alpha && R > 0)
                score = -search(newDepth, -alpha - 1, -alpha, ply + 1, false, m);
            if (score > alpha && score < beta)
                score = -search(newDepth, -beta, -alpha, ply + 1, true, m);
        }
        pos.undo_move(m);

        if (stop.load(std::memory_order_relaxed)) return 0;

        if (score > best) {
            best = score;
            bestMoveLocal = m;
            if (pvNode) {
                pvTable[ply][0] = m;
                for (int k = 0; k < pvLen[ply + 1]; ++k) pvTable[ply][k + 1] = pvTable[ply + 1][k];
                pvLen[ply] = pvLen[ply + 1] + 1;
            }
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) {   // beta cutoff
                    if (quiet) update_history(ply, m, quiets, nq, depth, us, prevMove);
                    break;
                }
            }
        }
    }

    if (legal == 0) return inCheck ? mated_in(ply) : 0;   // checkmate or stalemate

    Bound b = (best >= beta) ? BOUND_LOWER : (best > origAlpha ? BOUND_EXACT : BOUND_UPPER);
    tt.store(pos.key, bestMoveLocal, (best >= VALUE_MATE_IN_MAX) ? best + ply :
                                     (best <= -VALUE_MATE_IN_MAX ? best - ply : best),
             inCheck ? VALUE_NONE : eval, depth, b);
    return best;
}

// ---------------------------------------------------------------------------
//  Root search (iterates the persistent rootMoves list, PVS)
// ---------------------------------------------------------------------------
inline int Searcher::search_root(int alpha, int beta) {
    int best = -VALUE_INF;
    for (size_t idx = 0; idx < rootMoves.size(); ++idx) {
        RootMove& rm = rootMoves[idx];
        Move m = rm.move;
        bool givesCheck = pos.gives_check(m);
        int nd = rootDepth - 1 + (givesCheck ? 1 : 0);
        pos.do_move(m);
        int score;
        if (idx == 0) {
            score = -search(nd, -beta, -alpha, 1, true, m);
        } else {
            score = -search(nd, -alpha - 1, -alpha, 1, false, m);
            if (!stop.load(std::memory_order_relaxed) && score > alpha && score < beta)
                score = -search(nd, -beta, -alpha, 1, true, m);
        }
        pos.undo_move(m);
        if (stop.load(std::memory_order_relaxed)) return best;

        rm.score = score;
        rm.pv[0] = m;
        int L = pvLen[1];
        for (int k = 0; k < L; ++k) rm.pv[k + 1] = pvTable[1][k];
        rm.pvLen = L + 1;

        if (score > best) {
            best = score;
            if (score > alpha) alpha = score;
            if (score >= beta) break;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
//  Iterative deepening driver
// ---------------------------------------------------------------------------
inline std::string Searcher::pv_string() {
    std::string s;
    for (int i = 0; i < pvLen[0]; ++i) { if (i) s += ' '; s += move_to_uci(pvTable[0][i]); }
    return s;
}

inline void Searcher::think() {
    startTime = now_ms();
    nodes = 0;
    set_time_management();
    tt.new_generation();

    // Generate legal root moves
    rootMoves.clear();
    MoveList ml; pos.gen_pseudo(ml);
    for (int i = 0; i < ml.count; ++i) {
        Move m = ml.moves[i].move;
        if (pos.is_legal(m)) { RootMove rm; rm.move = m; rm.score = -VALUE_INF; rm.prevScore = -VALUE_INF; rm.pvLen = 0; rootMoves.push_back(rm); }
    }
    if (rootMoves.empty()) { if (!silent) { printf("bestmove 0000\n"); fflush(stdout); } return; }
    bestMove = rootMoves[0].move;

    int maxDepth = limits.depth > 0 ? std::min(limits.depth, MAX_PLY - 1) : MAX_PLY - 1;
    int prevScore = 0;

    for (rootDepth = 1; rootDepth <= maxDepth; ++rootDepth) {
        selDepth = 0;
        // Aspiration windows
        int alpha = -VALUE_INF, beta = VALUE_INF, delta = 25;
        if (rootDepth >= 4) { alpha = std::max(-VALUE_INF, prevScore - delta); beta = std::min(VALUE_INF, prevScore + delta); }

        int score = 0;
        while (true) {
            score = search_root(alpha, beta);
            if (stop.load(std::memory_order_relaxed)) break;
            if (score <= alpha) { beta = (alpha + beta) / 2; alpha = std::max(-VALUE_INF, score - delta); }
            else if (score >= beta) { beta = std::min(VALUE_INF, score + delta); }
            else break;
            delta += delta / 2 + 5;
        }

        if (stop.load(std::memory_order_relaxed) && rootDepth > 1) break;

        // sort root moves by score (best first) for next iteration & pick best
        std::stable_sort(rootMoves.begin(), rootMoves.end(),
                         [](const RootMove& a, const RootMove& b){ return a.score > b.score; });
        bestMove = rootMoves[0].move;
        prevScore = rootMoves[0].score;

        // info line
        int64_t elapsed = now_ms() - startTime;
        int sc = rootMoves[0].score;
        std::string scoreStr;
        if (std::abs(sc) >= VALUE_MATE_IN_MAX) {
            int matePlies = VALUE_MATE - std::abs(sc);
            int mateMoves = (matePlies + 1) / 2;
            scoreStr = "mate " + std::to_string(sc > 0 ? mateMoves : -mateMoves);
        } else scoreStr = "cp " + std::to_string(sc);

        std::string pvStr;
        for (int i = 0; i < rootMoves[0].pvLen; ++i) { if (i) pvStr += ' '; pvStr += move_to_uci(rootMoves[0].pv[i]); }
        if (pvStr.empty()) pvStr = move_to_uci(bestMove);

        uint64_t nps = elapsed > 0 ? (nodes * 1000 / elapsed) : nodes;
        if (!silent) {
            printf("info depth %d seldepth %d score %s nodes %llu nps %llu time %lld pv %s\n",
                   rootDepth, selDepth, scoreStr.c_str(),
                   (unsigned long long)nodes, (unsigned long long)nps, (long long)elapsed, pvStr.c_str());
            fflush(stdout);
        }

        // stop conditions between iterations
        if (limits.nodes && nodes >= limits.nodes) break;
        if (useTime && (now_ms() - startTime) >= softLimit) break;
        if (std::abs(sc) >= VALUE_MATE_IN_MAX && rootDepth >= 4) break; // found forced mate
    }

    if (!silent) { printf("bestmove %s\n", move_to_uci(bestMove).c_str()); fflush(stdout); }
}
