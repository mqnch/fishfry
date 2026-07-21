#pragma once
// ============================================================================
//  Evaluation: tapered (midgame/endgame) material + piece-square tables,
//  plus pawn structure, mobility, bishop pair, rook files, king safety, tempo.
//  Score is returned from the side-to-move's perspective (positive = better).
// ============================================================================
#include "position.h"

// Piece phase weights (for MG<->EG interpolation). Max = 24 at the start.
inline const int PhaseW[6] = { 0, 1, 1, 2, 4, 0 };
constexpr int MAX_PHASE = 24;

// Material (from PeSTO); folded together with PST below.
inline const int MgValue[6] = { 82, 337, 365, 477, 1025, 0 };
inline const int EgValue[6] = { 94, 281, 297, 512,  936, 0 };

// --- Piece-square tables (PeSTO), written rank 8 (a8) first --------------
inline const int MgPST[6][64] = {
  { // Pawn
      0,   0,   0,   0,   0,   0,   0,   0,
     98, 134,  61,  95,  68, 126,  34, -11,
     -6,   7,  26,  31,  65,  56,  25, -20,
    -14,  13,   6,  21,  23,  12,  17, -23,
    -27,  -2,  -5,  12,  17,   6,  10, -25,
    -26,  -4,  -4, -10,   3,   3,  33, -12,
    -35,  -1, -20, -23, -15,  24,  38, -22,
      0,   0,   0,   0,   0,   0,   0,   0 },
  { // Knight
   -167, -89, -34, -49,  61, -97, -15,-107,
    -73, -41,  72,  36,  23,  62,   7, -17,
    -47,  60,  37,  65,  84, 129,  73,  44,
     -9,  17,  19,  53,  37,  69,  18,  22,
    -13,   4,  16,  13,  28,  19,  21,  -8,
    -23,  -9,  12,  10,  19,  17,  25, -16,
    -29, -53, -12,  -3,  -1,  18, -14, -19,
   -105, -21, -58, -33, -17, -28, -19, -23 },
  { // Bishop
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
     -4,   5,  19,  50,  37,  37,   7,  -2,
     -6,  13,  13,  26,  34,  12,  10,   4,
      0,  15,  15,  15,  14,  27,  18,  10,
      4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21 },
  { // Rook
     32,  42,  32,  51,  63,   9,  31,  43,
     27,  32,  58,  62,  80,  67,  26,  44,
     -5,  19,  26,  36,  17,  45,  61,  16,
    -24, -11,   7,  26,  24,  35,  -8, -20,
    -36, -26, -12,  -1,   9,  -7,   6, -23,
    -45, -25, -16, -17,   3,   0,  -5, -33,
    -44, -16, -20,  -9,  -1,  11,  -6, -71,
    -19, -13,   1,  17,  16,   7, -37, -26 },
  { // Queen
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
     -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
     -1, -18,  -9,  10, -15, -25, -31, -50 },
  { // King
    -65,  23,  16, -15, -56, -34,   2,  13,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
     -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14 }
};
inline const int EgPST[6][64] = {
  { // Pawn
      0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
     94, 100,  85,  67,  56,  53,  82,  84,
     32,  24,  13,   5,  -2,   4,  17,  17,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   8,   8,  10,  13,   0,   2,  -7,
      0,   0,   0,   0,   0,   0,   0,   0 },
  { // Knight
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -15, -22, -18, -50, -64 },
  { // Bishop
    -14, -21, -11,  -8,  -7,  -9, -17, -24,
     -8,  -4,   7, -12,  -3, -13,  -4, -14,
      2,  -8,   0,  -1,  -2,   6,   0,   4,
     -3,   9,  12,   9,  14,  10,   3,   2,
     -6,   3,  13,  19,   7,  10,  -3,  -9,
    -12,  -3,   8,  10,  13,   3,  -7, -15,
    -14, -18,  -7,  -1,   4,  -9, -15, -27,
    -23,  -9, -23,  -5,  -9, -16,  -5, -17 },
  { // Rook
     13,  10,  18,  15,  12,  12,   8,   5,
     11,  13,  13,  11,  -3,   3,   8,   3,
      7,   7,   7,   5,   4,  -3,  -5,  -3,
      4,   3,  13,   1,   2,   1,  -1,   2,
      3,   5,   8,   4,  -5,  -6,  -8, -11,
     -4,   0,  -5,  -1,  -7, -12,  -8, -16,
     -6,  -6,   0,   2,  -9,  -9, -11,  -3,
     -9,   2,   3,  -1,  -5, -13,   4, -20 },
  { // Queen
     -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
      3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  39,  23,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -22, -23, -30, -16, -16, -23, -36, -32,
    -33, -28, -22, -43,  -5, -32, -20, -41 },
  { // King
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,
     -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43 }
};

// Precomputed pawn-structure masks
inline Bitboard PassedMask[2][64];  // squares that must be free of enemy pawns
inline Bitboard IsoMask[8];         // adjacent files
inline Bitboard FrontFile[2][64];   // squares ahead on same file (exclusive)
inline Bitboard KingRing[64];

inline void init_eval() {
    for (int f = 0; f < 8; ++f) {
        IsoMask[f] = 0;
        if (f > 0) IsoMask[f] |= file_bb(f - 1);
        if (f < 7) IsoMask[f] |= file_bb(f + 1);
    }
    for (int s = 0; s < 64; ++s) {
        int f = file_of(s), r = rank_of(s);
        // white front (ranks above r), black front (ranks below r)
        Bitboard wf = 0, bf = 0;
        for (int rr = r + 1; rr <= 7; ++rr) wf |= sq_bb(make_sq(f, rr));
        for (int rr = r - 1; rr >= 0; --rr) bf |= sq_bb(make_sq(f, rr));
        FrontFile[WHITE][s] = wf;
        FrontFile[BLACK][s] = bf;
        Bitboard adj = IsoMask[f] | file_bb(f);
        PassedMask[WHITE][s] = 0; PassedMask[BLACK][s] = 0;
        for (int rr = r + 1; rr <= 7; ++rr) PassedMask[WHITE][s] |= (adj & rank_bb(rr));
        for (int rr = r - 1; rr >= 0; --rr) PassedMask[BLACK][s] |= (adj & rank_bb(rr));
        KingRing[s] = KingAttacks[s] | sq_bb(s);
    }
}

// tuning constants
inline const int BishopPairMg = 22,  BishopPairEg = 40;
inline const int PassedBonusMg[8] = {0, 5, 10, 18, 35, 65, 110, 0};
inline const int PassedBonusEg[8] = {0, 10, 18, 33, 60, 105, 170, 0};
inline const int IsolatedMg = -12, IsolatedEg = -8;
inline const int DoubledMg  = -8,  DoubledEg  = -18;
inline const int RookOpenMg = 26,  RookOpenEg = 12;
inline const int RookSemiMg = 12,  RookSemiEg = 8;
inline const int TempoBonus = 14;
// linear mobility weights [mg,eg] for N,B,R,Q; count offset subtracts an "average"
inline const int MobMg[6] = {0, 4, 4, 2, 1, 0};
inline const int MobEg[6] = {0, 4, 3, 4, 2, 0};
inline const int MobOffset[6] = {0, 4, 6, 7, 14, 0};
inline const int KingOpenFilePenaltyMg = 18; // per open/semi-open file next to king

struct EvalAcc { int mg[2] = {0,0}, eg[2] = {0,0}; };

inline int evaluate(const Position& pos) {
    if (pos.is_material_draw()) return 0;

    EvalAcc a;
    int phase = 0;
    Bitboard occ = pos.pieces();

    Bitboard wpawns = pos.pieces(WHITE, PAWN);
    Bitboard bpawns = pos.pieces(BLACK, PAWN);
    Bitboard pawnsBB[2] = { wpawns, bpawns };

    for (int c = WHITE; c <= BLACK; ++c) {
        Color us = Color(c);
        Bitboard ourPawns = pawnsBB[c];
        Bitboard theirPawns = pawnsBB[c ^ 1];
        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard b = pos.pieces(us, PieceType(pt));
            phase += PhaseW[pt] * popcount(b);
            while (b) {
                int s = pop_lsb(b);
                int idx = (us == WHITE) ? (s ^ 56) : s;
                a.mg[c] += MgValue[pt] + MgPST[pt][idx];
                a.eg[c] += EgValue[pt] + EgPST[pt][idx];

                if (pt == PAWN) {
                    int f = file_of(s);
                    if (!(PassedMask[us][s] & theirPawns)) {
                        int relRank = (us == WHITE) ? rank_of(s) : 7 - rank_of(s);
                        a.mg[c] += PassedBonusMg[relRank];
                        a.eg[c] += PassedBonusEg[relRank];
                    }
                    if (!(IsoMask[f] & ourPawns)) { a.mg[c] += IsolatedMg; a.eg[c] += IsolatedEg; }
                    if (FrontFile[us][s] & ourPawns) { a.mg[c] += DoubledMg; a.eg[c] += DoubledEg; }
                }
                else if (pt >= KNIGHT && pt <= QUEEN) {
                    Bitboard att = attacks_of(PieceType(pt), s, occ) & ~pos.pieces(us);
                    int m = popcount(att) - MobOffset[pt];
                    a.mg[c] += m * MobMg[pt];
                    a.eg[c] += m * MobEg[pt];
                    if (pt == ROOK) {
                        int f = file_of(s);
                        if (!(file_bb(f) & (ourPawns | theirPawns))) { a.mg[c] += RookOpenMg; a.eg[c] += RookOpenEg; }
                        else if (!(file_bb(f) & ourPawns))           { a.mg[c] += RookSemiMg; a.eg[c] += RookSemiEg; }
                    }
                }
            }
        }
        if (popcount(pos.pieces(us, BISHOP)) >= 2) { a.mg[c] += BishopPairMg; a.eg[c] += BishopPairEg; }

        // simple king safety: penalise open/semi-open files around our king (MG only)
        int ksq = pos.king_sq(us);
        int kf = file_of(ksq);
        for (int df = -1; df <= 1; ++df) {
            int nf = kf + df;
            if (nf < 0 || nf > 7) continue;
            if (!(file_bb(nf) & ourPawns)) a.mg[c] -= KingOpenFilePenaltyMg;
        }
    }

    if (phase > MAX_PHASE) phase = MAX_PHASE;
    int mgScore = a.mg[WHITE] - a.mg[BLACK];
    int egScore = a.eg[WHITE] - a.eg[BLACK];
    int score = (mgScore * phase + egScore * (MAX_PHASE - phase)) / MAX_PHASE;

    int stm = (pos.to_move() == WHITE) ? score : -score;
    return stm + TempoBonus;
}
