#pragma once
// ============================================================================
//  Bitboard primitives, types, move encoding, and magic-bitboard attack tables
//  Squares: A1=0, B1=1, ... H1=7, A2=8, ... H8=63  (little-endian rank-file)
//  rank = sq >> 3, file = sq & 7
// ============================================================================
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using Bitboard = uint64_t;
using Move     = uint16_t;

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
enum PieceType : int { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_TYPE_NB = 6, NO_PIECE_TYPE = 6 };
// Piece codes 0..11 = color*6 + type ; 12 = empty
enum Piece : int { W_PAWN=0, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
                   B_PAWN=6, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
                   NO_PIECE = 12 };

constexpr int SQUARE_NB = 64;
enum Square : int { SQ_A1=0, SQ_NONE = 64 };

// Castling-right bitmask
enum CastlingRight { WK = 1, WQ = 2, BK = 4, BQ = 8 };

// Directions (as square deltas) — used carefully with file guards
enum Direction : int { NORTH = 8, SOUTH = -8, EAST = 1, WEST = -1,
                       NE = 9, NW = 7, SE = -7, SW = -9 };

constexpr Color operator~(Color c) { return Color(c ^ 1); }

// ---- basic bit ops (portable, work on arm64 + x86) ----
inline int  popcount(Bitboard b) { return __builtin_popcountll(b); }
inline int  lsb(Bitboard b)      { return __builtin_ctzll(b); }        // b != 0
inline int  msb(Bitboard b)      { return 63 ^ __builtin_clzll(b); }   // b != 0
inline int  pop_lsb(Bitboard& b) { int s = lsb(b); b &= b - 1; return s; }

inline Bitboard sq_bb(int s) { return 1ULL << s; }
inline int   file_of(int s) { return s & 7; }
inline int   rank_of(int s) { return s >> 3; }
inline int   make_sq(int f, int r) { return r * 8 + f; }
inline bool  is_ok(int s) { return s >= 0 && s < 64; }

// Chebyshev distance between squares (for king-safety / eval)
inline int   file_dist(int a, int b) { int d = file_of(a) - file_of(b); return d < 0 ? -d : d; }
inline int   rank_dist(int a, int b) { int d = rank_of(a) - rank_of(b); return d < 0 ? -d : d; }
inline int   sq_dist(int a, int b)   { int f = file_dist(a,b), r = rank_dist(a,b); return f > r ? f : r; }

constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileHBB = 0x8080808080808080ULL;
constexpr Bitboard Rank1BB = 0x00000000000000FFULL;
constexpr Bitboard Rank2BB = Rank1BB << 8;
constexpr Bitboard Rank3BB = Rank1BB << 16;
constexpr Bitboard Rank4BB = Rank1BB << 24;
constexpr Bitboard Rank5BB = Rank1BB << 32;
constexpr Bitboard Rank6BB = Rank1BB << 40;
constexpr Bitboard Rank7BB = Rank1BB << 48;
constexpr Bitboard Rank8BB = Rank1BB << 56;

inline Bitboard file_bb(int f) { return FileABB << f; }
inline Bitboard rank_bb(int r) { return Rank1BB << (8 * r); }

// Shift a bitboard one step in a direction, masking wrap-around on files.
template<Direction D>
inline Bitboard shift(Bitboard b) {
    switch (D) {
        case NORTH: return b << 8;
        case SOUTH: return b >> 8;
        case EAST:  return (b & ~FileHBB) << 1;
        case WEST:  return (b & ~FileABB) >> 1;
        case NE:    return (b & ~FileHBB) << 9;
        case NW:    return (b & ~FileABB) << 7;
        case SE:    return (b & ~FileHBB) >> 7;
        case SW:    return (b & ~FileABB) >> 9;
    }
    return 0;
}

// ---- Move encoding (16 bits) ----
//  bits 0-5   from
//  bits 6-11  to
//  bits 12-13 promotion piece (0=N,1=B,2=R,3=Q)
//  bits 14-15 move type: 0 normal, 1 promotion, 2 en passant, 3 castling
enum MoveType { MT_NORMAL = 0, MT_PROMO = 1 << 14, MT_ENPASSANT = 2 << 14, MT_CASTLE = 3 << 14 };
constexpr Move MOVE_NONE = 0;
constexpr Move MOVE_NULL = 65; // from==to==1 (b1), harmless sentinel

inline Move make_move(int from, int to) { return Move(from | (to << 6)); }
inline Move make_move(int from, int to, MoveType mt) { return Move(from | (to << 6) | mt); }
inline Move make_promo(int from, int to, PieceType pt) {
    return Move(from | (to << 6) | ((pt - KNIGHT) << 12) | MT_PROMO);
}
inline int       from_sq(Move m)  { return m & 0x3F; }
inline int       to_sq(Move m)    { return (m >> 6) & 0x3F; }
inline MoveType  type_of(Move m)  { return MoveType(m & (3 << 14)); }
inline PieceType promo_of(Move m) { return PieceType(KNIGHT + ((m >> 12) & 3)); }

// ============================================================================
//  Attack tables
// ============================================================================
inline Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
inline Bitboard KnightAttacks[SQUARE_NB];
inline Bitboard KingAttacks[SQUARE_NB];
inline Bitboard BetweenBB[SQUARE_NB][SQUARE_NB]; // strictly-between (exclusive), else 0
inline Bitboard LineBB[SQUARE_NB][SQUARE_NB];    // full line through a,b (incl both), else 0

struct Magic {
    Bitboard  mask;
    Bitboard  magic;
    Bitboard* attacks;
    unsigned  shift;
    inline unsigned index(Bitboard occ) const {
        return unsigned(((occ & mask) * magic) >> shift);
    }
};
inline Magic BishopMagics[SQUARE_NB];
inline Magic RookMagics[SQUARE_NB];
inline Bitboard BishopTable[5248];
inline Bitboard RookTable[102400];

// Slow reference sliding attacks (used only at init).
inline Bitboard sliding_attacks_slow(int sq, Bitboard occ, const int dirs[4][2]) {
    Bitboard att = 0;
    int f0 = file_of(sq), r0 = rank_of(sq);
    for (int d = 0; d < 4; ++d) {
        int df = dirs[d][0], dr = dirs[d][1];
        int f = f0 + df, r = r0 + dr;
        while (f >= 0 && f <= 7 && r >= 0 && r <= 7) {
            int s = make_sq(f, r);
            att |= sq_bb(s);
            if (occ & sq_bb(s)) break;
            f += df; r += dr;
        }
    }
    return att;
}
inline const int RookDirs[4][2]   = {{1,0},{-1,0},{0,1},{0,-1}};
inline const int BishopDirs[4][2] = {{1,1},{-1,1},{1,-1},{-1,-1}};

inline Bitboard sliding_mask(int sq, const int dirs[4][2]) {
    // Like attacks over empty board but excluding the border squares.
    Bitboard m = 0;
    int f0 = file_of(sq), r0 = rank_of(sq);
    for (int d = 0; d < 4; ++d) {
        int df = dirs[d][0], dr = dirs[d][1];
        int f = f0 + df, r = r0 + dr;
        while (f >= 0 && f <= 7 && r >= 0 && r <= 7) {
            int nf = f + df, nr = r + dr;
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7) break; // stop before edge square
            m |= sq_bb(make_sq(f, r));
            f += df; r += dr;
        }
    }
    return m;
}

// Deterministic PRNG (xorshift64) for magic search.
struct PRNG {
    uint64_t s;
    PRNG(uint64_t seed) : s(seed) {}
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; }
    uint64_t sparse() { return next() & next() & next(); } // few set bits => good magic candidate
};

inline void init_magics(bool bishop) {
    Magic* table = bishop ? BishopMagics : RookMagics;
    Bitboard* pool = bishop ? BishopTable : RookTable;
    const int (*dirs)[2] = bishop ? BishopDirs : RookDirs;
    PRNG rng(bishop ? 0x1234567890ABCDEFULL : 0x0FEDCBA098765432ULL);

    size_t offset = 0;
    std::vector<Bitboard> occ(4096), ref(4096);
    for (int sq = 0; sq < 64; ++sq) {
        Magic& m = table[sq];
        m.mask  = sliding_mask(sq, dirs);
        int bits = popcount(m.mask);
        m.shift = 64 - bits;
        m.attacks = pool + offset;
        int size = 1 << bits;
        offset += size;

        // Enumerate all occupancy subsets of the mask (Carry-Rippler).
        Bitboard b = 0;
        int idx = 0;
        do {
            occ[idx] = b;
            ref[idx] = sliding_attacks_slow(sq, b, dirs);
            ++idx;
            b = (b - m.mask) & m.mask;
        } while (b);

        // Search for a magic that maps every subset without a (nonequal) collision.
        std::vector<Bitboard> used(size);
        while (true) {
            Bitboard magic;
            do { magic = rng.sparse(); } while (popcount((m.mask * magic) >> 56) < 6);
            m.magic = magic;
            std::fill(used.begin(), used.end(), 0ULL);
            bool ok = true;
            for (int i = 0; i < idx; ++i) {
                unsigned j = m.index(occ[i]);
                if (used[j] == 0ULL) used[j] = ref[i];
                else if (used[j] != ref[i]) { ok = false; break; }
            }
            if (ok) { for (int i = 0; i < idx; ++i) m.attacks[m.index(occ[i])] = ref[i]; break; }
        }
    }
}

inline Bitboard bishop_attacks(int sq, Bitboard occ) { const Magic& m = BishopMagics[sq]; return m.attacks[m.index(occ)]; }
inline Bitboard rook_attacks(int sq, Bitboard occ)   { const Magic& m = RookMagics[sq];   return m.attacks[m.index(occ)]; }
inline Bitboard queen_attacks(int sq, Bitboard occ)  { return bishop_attacks(sq, occ) | rook_attacks(sq, occ); }

inline Bitboard attacks_of(PieceType pt, int sq, Bitboard occ) {
    switch (pt) {
        case KNIGHT: return KnightAttacks[sq];
        case BISHOP: return bishop_attacks(sq, occ);
        case ROOK:   return rook_attacks(sq, occ);
        case QUEEN:  return queen_attacks(sq, occ);
        case KING:   return KingAttacks[sq];
        default:     return 0;
    }
}

inline void init_bitboards() {
    // Leaper attacks
    for (int s = 0; s < 64; ++s) {
        Bitboard bb = sq_bb(s);
        int f = file_of(s), r = rank_of(s);
        // pawns
        PawnAttacks[WHITE][s] = shift<NW>(bb) | shift<NE>(bb);
        PawnAttacks[BLACK][s] = shift<SW>(bb) | shift<SE>(bb);
        // knight
        Bitboard n = 0;
        const int km[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
        for (auto& d : km) { int nf=f+d[0], nr=r+d[1]; if (nf>=0&&nf<=7&&nr>=0&&nr<=7) n |= sq_bb(make_sq(nf,nr)); }
        KnightAttacks[s] = n;
        // king
        Bitboard k = 0;
        for (int df=-1; df<=1; ++df) for (int dr=-1; dr<=1; ++dr) {
            if (!df && !dr) continue;
            int nf=f+df, nr=r+dr; if (nf>=0&&nf<=7&&nr>=0&&nr<=7) k |= sq_bb(make_sq(nf,nr));
        }
        KingAttacks[s] = k;
    }
    init_magics(true);
    init_magics(false);

    // Between / Line tables
    for (int a = 0; a < 64; ++a)
        for (int b = 0; b < 64; ++b) {
            BetweenBB[a][b] = 0; LineBB[a][b] = 0;
            if (a == b) continue;
            if (rook_attacks(a, 0) & sq_bb(b)) {
                BetweenBB[a][b] = rook_attacks(a, sq_bb(b)) & rook_attacks(b, sq_bb(a));
                LineBB[a][b]    = (rook_attacks(a,0) & rook_attacks(b,0)) | sq_bb(a) | sq_bb(b);
            } else if (bishop_attacks(a, 0) & sq_bb(b)) {
                BetweenBB[a][b] = bishop_attacks(a, sq_bb(b)) & bishop_attacks(b, sq_bb(a));
                LineBB[a][b]    = (bishop_attacks(a,0) & bishop_attacks(b,0)) | sq_bb(a) | sq_bb(b);
            }
        }
}

// Square/UCI helpers
inline std::string sq_to_str(int s) { std::string r; r += char('a' + file_of(s)); r += char('1' + rank_of(s)); return r; }
inline int str_to_sq(const std::string& s) { return make_sq(s[0]-'a', s[1]-'1'); }
