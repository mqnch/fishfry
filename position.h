#pragma once
// ============================================================================
//  Position: board state, Zobrist hashing, make/unmake, attack queries, SEE.
// ============================================================================
#include "bitboard.h"
#include <sstream>
#include <cassert>

constexpr int MAX_GAME_PLY = 4096;

inline int  color_of(Piece p)      { return p >= B_PAWN ? BLACK : WHITE; }
inline PieceType ptype_of(Piece p) { return PieceType(p % 6); }
inline Piece make_piece(Color c, PieceType pt) { return Piece(c * 6 + pt); }

// Static-exchange / ordering piece values
inline const int SeeValue[7] = { 100, 320, 330, 500, 900, 20000, 0 };

// ---------------------------------------------------------------------------
//  Zobrist keys
// ---------------------------------------------------------------------------
struct Zobrist {
    uint64_t psq[12][64];
    uint64_t castle[16];
    uint64_t ep[8];
    uint64_t side;
};
inline Zobrist Zob;

inline void init_zobrist() {
    PRNG rng(0xDEADBEEFCAFEBABEULL);
    for (int p = 0; p < 12; ++p)
        for (int s = 0; s < 64; ++s) Zob.psq[p][s] = rng.next();
    for (int i = 0; i < 16; ++i) Zob.castle[i] = rng.next();
    for (int f = 0; f < 8; ++f)  Zob.ep[f] = rng.next();
    Zob.side = rng.next();
}

// ---------------------------------------------------------------------------
//  Move list
// ---------------------------------------------------------------------------
struct ScoredMove { Move move; int score; };
struct MoveList {
    ScoredMove moves[256];
    int count = 0;
    inline void add(Move m) { moves[count++].move = m; }
    ScoredMove* begin() { return moves; }
    ScoredMove* end()   { return moves + count; }
};

// ---------------------------------------------------------------------------
//  Undo record
// ---------------------------------------------------------------------------
struct Undo {
    int      castling;
    int      epSquare;
    int      rule50;
    uint64_t key;
    Piece    captured;
};

// ---------------------------------------------------------------------------
//  Position
// ---------------------------------------------------------------------------
class Position {
public:
    Bitboard byColor[2] = {0,0};
    Bitboard byType[6]  = {0,0,0,0,0,0};
    Piece    mailbox[64];
    Color    side = WHITE;
    int      castling = 0;
    int      epSquare = SQ_NONE;
    int      rule50 = 0;
    int      histPly = 0;          // half-moves since game start (for repetition)
    int      gamePlyRoot = 0;      // histPly at search root
    uint64_t key = 0;

    Undo     undoStack[MAX_GAME_PLY];
    int      undoCount = 0;
    uint64_t repKeys[MAX_GAME_PLY];

    Position() { for (int i = 0; i < 64; ++i) mailbox[i] = NO_PIECE; }

    // --- accessors ---
    inline Bitboard pieces() const { return byColor[WHITE] | byColor[BLACK]; }
    inline Bitboard pieces(Color c) const { return byColor[c]; }
    inline Bitboard pieces(PieceType pt) const { return byType[pt]; }
    inline Bitboard pieces(Color c, PieceType pt) const { return byColor[c] & byType[pt]; }
    inline Bitboard pieces(Color c, PieceType a, PieceType b) const { return byColor[c] & (byType[a] | byType[b]); }
    inline Piece    piece_on(int s) const { return mailbox[s]; }
    inline bool     empty(int s) const { return mailbox[s] == NO_PIECE; }
    inline int      king_sq(Color c) const { return lsb(pieces(c, KING)); }
    inline Color    to_move() const { return side; }

    // --- piece placement helpers (keep bitboards + mailbox + key in sync) ---
    inline void put_piece(Piece pc, int s) {
        mailbox[s] = pc;
        Bitboard b = sq_bb(s);
        byColor[color_of(pc)] |= b;
        byType[ptype_of(pc)]  |= b;
        key ^= Zob.psq[pc][s];
    }
    inline void remove_piece(int s) {
        Piece pc = mailbox[s];
        Bitboard b = sq_bb(s);
        byColor[color_of(pc)] ^= b;
        byType[ptype_of(pc)]  ^= b;
        key ^= Zob.psq[pc][s];
        mailbox[s] = NO_PIECE;
    }
    inline void move_piece(int from, int to) {
        Piece pc = mailbox[from];
        Bitboard fromto = sq_bb(from) ^ sq_bb(to);
        byColor[color_of(pc)] ^= fromto;
        byType[ptype_of(pc)]  ^= fromto;
        key ^= Zob.psq[pc][from] ^ Zob.psq[pc][to];
        mailbox[from] = NO_PIECE;
        mailbox[to]   = pc;
    }

    // --- attack queries ---
    inline Bitboard attackers_to(int s, Bitboard occ) const {
        return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN))
             | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN))
             | (KnightAttacks[s]      & byType[KNIGHT])
             | (KingAttacks[s]        & byType[KING])
             | (bishop_attacks(s, occ) & (byType[BISHOP] | byType[QUEEN]))
             | (rook_attacks(s, occ)   & (byType[ROOK]   | byType[QUEEN]));
    }
    inline bool attacked_by(int s, Color c, Bitboard occ) const {
        if (PawnAttacks[~c][s] & pieces(c, PAWN)) return true;
        if (KnightAttacks[s]   & pieces(c, KNIGHT)) return true;
        if (KingAttacks[s]     & pieces(c, KING)) return true;
        if (bishop_attacks(s, occ) & (pieces(c, BISHOP) | pieces(c, QUEEN))) return true;
        if (rook_attacks(s, occ)   & (pieces(c, ROOK)   | pieces(c, QUEEN))) return true;
        return false;
    }
    inline bool in_check() const { return attacked_by(king_sq(side), ~side, pieces()); }
    inline bool gives_check_sq(Color us, int ksq) const { return attacked_by(ksq, us, pieces()); }

    // --- FEN ---
    void set(const std::string& fen);
    std::string fen() const;

    // --- move generation (pseudo-legal) ---
    void gen_pseudo(MoveList& ml) const;
    bool is_legal(Move m) const;         // full legality test for a pseudo-legal move
    bool gives_check(Move m) const;      // does m give check to opponent (called before make)

    // --- make / unmake ---
    void do_move(Move m);
    void undo_move(Move m);
    void do_null();
    void undo_null();

    // --- draw detection ---
    bool is_repetition() const {
        int end = histPly - rule50;
        if (end < 0) end = 0;
        for (int i = histPly - 4; i >= end; i -= 2)
            if (repKeys[i] == key) return true;
        return false;
    }
    bool is_material_draw() const {
        if (byType[PAWN] | byType[ROOK] | byType[QUEEN]) return false;
        // Only minor pieces / kings left.
        int wN = popcount(pieces(WHITE, KNIGHT)), wB = popcount(pieces(WHITE, BISHOP));
        int bN = popcount(pieces(BLACK, KNIGHT)), bB = popcount(pieces(BLACK, BISHOP));
        int minors = wN + wB + bN + bB;
        if (minors <= 1) return true;                 // K vs K, K+minor vs K
        if (minors == 2 && wN==0 && bN==0 && wB==1 && bB==1) return true; // KB vs KB (any colors) - treat as draw-ish
        if (minors == 2 && ((wN==2&&wB==0&&bN==0&&bB==0)||(bN==2&&bB==0&&wN==0&&wB==0))) return true; // KNN vs K
        return false;
    }

    // --- SEE ---
    bool see_ge(Move m, int threshold) const;

    // helper for castling rook squares
    void do_castle(Color us, int kfrom, int kto, bool make);

    void print() const;
private:
    template<Color Us> void gen_pawn_moves(MoveList& ml) const;
    template<Color Us> void gen_piece_moves(MoveList& ml) const;
};

// castling rights update mask per square
inline int CastleMask[64];
inline void init_castle_mask() {
    for (int i = 0; i < 64; ++i) CastleMask[i] = 15;
    CastleMask[4]  = 15 & ~(WK | WQ); // e1
    CastleMask[0]  = 15 & ~WQ;        // a1
    CastleMask[7]  = 15 & ~WK;        // h1
    CastleMask[60] = 15 & ~(BK | BQ); // e8
    CastleMask[56] = 15 & ~BQ;        // a8
    CastleMask[63] = 15 & ~BK;        // h8
}

// ---------------------------------------------------------------------------
//  FEN parsing
// ---------------------------------------------------------------------------
inline void Position::set(const std::string& fen) {
    for (int i = 0; i < 64; ++i) mailbox[i] = NO_PIECE;
    byColor[0] = byColor[1] = 0;
    for (int i = 0; i < 6; ++i) byType[i] = 0;
    castling = 0; epSquare = SQ_NONE; rule50 = 0; histPly = 0; undoCount = 0; key = 0; side = WHITE;

    std::istringstream ss(fen);
    std::string board, stm, cr, ep;
    int half = 0, full = 1;
    ss >> board >> stm >> cr >> ep;
    if (!(ss >> half)) half = 0;
    if (!(ss >> full)) full = 1;

    int r = 7, f = 0;
    for (char c : board) {
        if (c == '/') { r--; f = 0; }
        else if (c >= '1' && c <= '8') f += c - '0';
        else {
            Piece pc = NO_PIECE;
            switch (c) {
                case 'P': pc=W_PAWN; break;   case 'N': pc=W_KNIGHT; break; case 'B': pc=W_BISHOP; break;
                case 'R': pc=W_ROOK; break;   case 'Q': pc=W_QUEEN; break;  case 'K': pc=W_KING; break;
                case 'p': pc=B_PAWN; break;   case 'n': pc=B_KNIGHT; break; case 'b': pc=B_BISHOP; break;
                case 'r': pc=B_ROOK; break;   case 'q': pc=B_QUEEN; break;  case 'k': pc=B_KING; break;
            }
            if (pc != NO_PIECE) { put_piece(pc, make_sq(f, r)); f++; }
        }
    }
    side = (stm == "b") ? BLACK : WHITE;
    for (char c : cr) {
        if (c=='K') castling |= WK; else if (c=='Q') castling |= WQ;
        else if (c=='k') castling |= BK; else if (c=='q') castling |= BQ;
    }
    if (ep != "-" && ep.size() >= 2) epSquare = str_to_sq(ep);
    rule50 = half;

    // finalize key with side/castle/ep
    if (side == BLACK) key ^= Zob.side;
    key ^= Zob.castle[castling];
    if (epSquare != SQ_NONE) key ^= Zob.ep[file_of(epSquare)];

    histPly = 0;
    repKeys[0] = key;
    gamePlyRoot = 0;
}

inline std::string Position::fen() const {
    std::string s;
    for (int r = 7; r >= 0; --r) {
        int emptyc = 0;
        for (int f = 0; f < 8; ++f) {
            Piece pc = mailbox[make_sq(f,r)];
            if (pc == NO_PIECE) { emptyc++; continue; }
            if (emptyc) { s += char('0'+emptyc); emptyc = 0; }
            const char* L = "PNBRQKpnbrqk";
            s += L[pc];
        }
        if (emptyc) s += char('0'+emptyc);
        if (r) s += '/';
    }
    s += side == WHITE ? " w " : " b ";
    std::string cr;
    if (castling & WK) cr+='K'; if (castling & WQ) cr+='Q';
    if (castling & BK) cr+='k'; if (castling & BQ) cr+='q';
    s += cr.empty() ? "-" : cr;
    s += ' ';
    s += epSquare==SQ_NONE ? "-" : sq_to_str(epSquare);
    return s;
}

// ---------------------------------------------------------------------------
//  Pseudo-legal move generation
// ---------------------------------------------------------------------------
template<Color Us>
inline void Position::gen_pawn_moves(MoveList& ml) const {
    constexpr Color Them = ~Us;
    constexpr Direction Up      = (Us == WHITE) ? NORTH : SOUTH;
    constexpr Direction UpLeft  = (Us == WHITE) ? NW : SE;   // capture toward file A side for white(+7)/... handled via delta
    constexpr Direction UpRight = (Us == WHITE) ? NE : SW;
    constexpr Bitboard  Rank7   = (Us == WHITE) ? Rank7BB : Rank2BB;
    constexpr Bitboard  Rank3   = (Us == WHITE) ? Rank3BB : Rank6BB;
    const int dUp = Up, dL = UpLeft, dR = UpRight;

    Bitboard pawns  = pieces(Us, PAWN);
    Bitboard empty  = ~pieces();
    Bitboard enemies= byColor[Them];
    Bitboard promo  = pawns & Rank7;
    Bitboard nonpr  = pawns & ~Rank7;

    // quiet pushes
    Bitboard push1 = shift<Up>(nonpr) & empty;
    Bitboard push2 = shift<Up>(push1 & Rank3) & empty;
    for (Bitboard b = push1; b; ) { int to = pop_lsb(b); ml.add(make_move(to - dUp, to)); }
    for (Bitboard b = push2; b; ) { int to = pop_lsb(b); ml.add(make_move(to - 2*dUp, to)); }

    // promotions (push)
    Bitboard prPush = shift<Up>(promo) & empty;
    for (Bitboard b = prPush; b; ) {
        int to = pop_lsb(b), from = to - dUp;
        ml.add(make_promo(from, to, QUEEN)); ml.add(make_promo(from, to, ROOK));
        ml.add(make_promo(from, to, BISHOP)); ml.add(make_promo(from, to, KNIGHT));
    }

    // captures (non-promoting)
    Bitboard capL = shift<UpLeft>(nonpr)  & enemies;
    Bitboard capR = shift<UpRight>(nonpr) & enemies;
    for (Bitboard b = capL; b; ) { int to = pop_lsb(b); ml.add(make_move(to - dL, to)); }
    for (Bitboard b = capR; b; ) { int to = pop_lsb(b); ml.add(make_move(to - dR, to)); }

    // capture-promotions
    Bitboard capLP = shift<UpLeft>(promo)  & enemies;
    Bitboard capRP = shift<UpRight>(promo) & enemies;
    for (Bitboard b = capLP; b; ) { int to = pop_lsb(b), from = to - dL;
        ml.add(make_promo(from,to,QUEEN)); ml.add(make_promo(from,to,ROOK)); ml.add(make_promo(from,to,BISHOP)); ml.add(make_promo(from,to,KNIGHT)); }
    for (Bitboard b = capRP; b; ) { int to = pop_lsb(b), from = to - dR;
        ml.add(make_promo(from,to,QUEEN)); ml.add(make_promo(from,to,ROOK)); ml.add(make_promo(from,to,BISHOP)); ml.add(make_promo(from,to,KNIGHT)); }

    // en passant
    if (epSquare != SQ_NONE) {
        Bitboard eppawns = pawns & PawnAttacks[Them][epSquare];
        for (Bitboard b = eppawns; b; ) { int from = pop_lsb(b); ml.add(make_move(from, epSquare, MT_ENPASSANT)); }
    }
}

template<Color Us>
inline void Position::gen_piece_moves(MoveList& ml) const {
    Bitboard occ = pieces();
    Bitboard own = byColor[Us];
    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) {
        Bitboard b = pieces(Us, pt);
        while (b) {
            int from = pop_lsb(b);
            Bitboard att = attacks_of(pt, from, occ) & ~own;
            while (att) { int to = pop_lsb(att); ml.add(make_move(from, to)); }
        }
    }
    // king
    int ksq = king_sq(Us);
    Bitboard katt = KingAttacks[ksq] & ~own;
    while (katt) { int to = pop_lsb(katt); ml.add(make_move(ksq, to)); }

    // castling
    constexpr int kHome = (Us==WHITE) ? 4 : 60;
    if (Us == WHITE) {
        if ((castling & WK) && !(occ & (sq_bb(5)|sq_bb(6))) &&
            !attacked_by(4,BLACK,occ) && !attacked_by(5,BLACK,occ) && !attacked_by(6,BLACK,occ))
            ml.add(make_move(4, 6, MT_CASTLE));
        if ((castling & WQ) && !(occ & (sq_bb(1)|sq_bb(2)|sq_bb(3))) &&
            !attacked_by(4,BLACK,occ) && !attacked_by(3,BLACK,occ) && !attacked_by(2,BLACK,occ))
            ml.add(make_move(4, 2, MT_CASTLE));
    } else {
        if ((castling & BK) && !(occ & (sq_bb(61)|sq_bb(62))) &&
            !attacked_by(60,WHITE,occ) && !attacked_by(61,WHITE,occ) && !attacked_by(62,WHITE,occ))
            ml.add(make_move(60, 62, MT_CASTLE));
        if ((castling & BQ) && !(occ & (sq_bb(57)|sq_bb(58)|sq_bb(59))) &&
            !attacked_by(60,WHITE,occ) && !attacked_by(59,WHITE,occ) && !attacked_by(58,WHITE,occ))
            ml.add(make_move(60, 58, MT_CASTLE));
    }
    (void)kHome;
}

inline void Position::gen_pseudo(MoveList& ml) const {
    if (side == WHITE) { gen_pawn_moves<WHITE>(ml); gen_piece_moves<WHITE>(ml); }
    else               { gen_pawn_moves<BLACK>(ml); gen_piece_moves<BLACK>(ml); }
}

// full legality check for a pseudo-legal move
inline bool Position::is_legal(Move m) const {
    Color us = side, them = ~side;
    int from = from_sq(m), to = to_sq(m);
    MoveType mt = type_of(m);
    int ksq = king_sq(us);

    if (mt == MT_ENPASSANT) {
        // Make the ep capture on a copy of occupancy and test king safety.
        int capSq = (us == WHITE) ? to - 8 : to + 8;
        Bitboard occ = (pieces() ^ sq_bb(from) ^ sq_bb(capSq)) | sq_bb(to);
        return !(rook_attacks(ksq, occ) & (pieces(them, ROOK, QUEEN)))
            && !(bishop_attacks(ksq, occ) & (pieces(them, BISHOP, QUEEN)));
    }
    if (piece_on(from) == make_piece(us, KING) && mt != MT_CASTLE) {
        // king move (non-castle): destination must not be attacked with king removed
        Bitboard occ = pieces() ^ sq_bb(from);
        return !attacked_by(to, them, occ);
    }
    if (mt == MT_CASTLE) return true; // path/attack already validated during generation
    // Non-king move: legal iff king not attacked afterwards. Remove 'from', occupy 'to',
    // and exclude any just-captured enemy piece (which sits on 'to') from attacker sets.
    Bitboard occ = (pieces() ^ sq_bb(from)) | sq_bb(to);
    Bitboard notCap = ~sq_bb(to);
    return !(rook_attacks(ksq, occ)   & pieces(them, ROOK, QUEEN)   & notCap)
        && !(bishop_attacks(ksq, occ) & pieces(them, BISHOP, QUEEN) & notCap)
        && !(KnightAttacks[ksq]       & pieces(them, KNIGHT)        & notCap)
        && !(PawnAttacks[us][ksq]     & pieces(them, PAWN)          & notCap)
        && !(KingAttacks[ksq]         & pieces(them, KING));
}

// ---------------------------------------------------------------------------
//  Castling helper
// ---------------------------------------------------------------------------
inline void Position::do_castle(Color us, int kfrom, int kto, bool make) {
    bool kingside = kto > kfrom;
    int rfrom = kingside ? (us==WHITE ? 7 : 63) : (us==WHITE ? 0 : 56);
    int rto   = kingside ? (kto - 1) : (kto + 1);
    if (make) { move_piece(kfrom, kto); move_piece(rfrom, rto); }
    else      { move_piece(kto, kfrom); move_piece(rto, rfrom); }
}

// ---------------------------------------------------------------------------
//  make / unmake
// ---------------------------------------------------------------------------
inline void Position::do_move(Move m) {
    Undo& u = undoStack[undoCount++];
    u.castling = castling; u.epSquare = epSquare; u.rule50 = rule50; u.key = key; u.captured = NO_PIECE;

    Color us = side, them = ~side;
    int from = from_sq(m), to = to_sq(m);
    MoveType mt = type_of(m);
    Piece pc = mailbox[from];
    PieceType pt = ptype_of(pc);

    if (epSquare != SQ_NONE) { key ^= Zob.ep[file_of(epSquare)]; epSquare = SQ_NONE; }
    ++rule50;

    if (mt == MT_CASTLE) {
        do_castle(us, from, to, true);
    } else if (mt == MT_ENPASSANT) {
        int capSq = (us == WHITE) ? to - 8 : to + 8;
        u.captured = mailbox[capSq];
        remove_piece(capSq);
        move_piece(from, to);
        rule50 = 0;
    } else {
        if (mailbox[to] != NO_PIECE) { u.captured = mailbox[to]; remove_piece(to); rule50 = 0; }
        move_piece(from, to);
        if (pt == PAWN) {
            rule50 = 0;
            if (mt == MT_PROMO) {
                remove_piece(to);
                put_piece(make_piece(us, promo_of(m)), to);
            } else if (to - from == 16 || from - to == 16) {
                int ep = (from + to) / 2;
                if (PawnAttacks[us][ep] & pieces(them, PAWN)) {
                    epSquare = ep;
                    key ^= Zob.ep[file_of(ep)];
                }
            }
        }
    }

    // castling rights
    int newCastling = castling & CastleMask[from] & CastleMask[to];
    if (newCastling != castling) { key ^= Zob.castle[castling] ^ Zob.castle[newCastling]; castling = newCastling; }

    side = them; key ^= Zob.side;
    ++histPly; repKeys[histPly] = key;
}

inline void Position::undo_move(Move m) {
    side = ~side;
    Color us = side;
    int from = from_sq(m), to = to_sq(m);
    MoveType mt = type_of(m);

    if (mt == MT_CASTLE) {
        do_castle(us, from, to, false);
    } else if (mt == MT_ENPASSANT) {
        move_piece(to, from);
        int capSq = (us == WHITE) ? to - 8 : to + 8;
        put_piece(undoStack[undoCount-1].captured, capSq);
    } else {
        if (mt == MT_PROMO) {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }
        move_piece(to, from);
        if (undoStack[undoCount-1].captured != NO_PIECE)
            put_piece(undoStack[undoCount-1].captured, to);
    }

    Undo& u = undoStack[--undoCount];
    castling = u.castling; epSquare = u.epSquare; rule50 = u.rule50; key = u.key;
    --histPly;
}

inline void Position::do_null() {
    Undo& u = undoStack[undoCount++];
    u.castling = castling; u.epSquare = epSquare; u.rule50 = rule50; u.key = key; u.captured = NO_PIECE;
    if (epSquare != SQ_NONE) { key ^= Zob.ep[file_of(epSquare)]; epSquare = SQ_NONE; }
    ++rule50;
    side = ~side; key ^= Zob.side;
    ++histPly; repKeys[histPly] = key;
}
inline void Position::undo_null() {
    side = ~side;
    Undo& u = undoStack[--undoCount];
    castling = u.castling; epSquare = u.epSquare; rule50 = u.rule50; key = u.key;
    --histPly;
}

// ---------------------------------------------------------------------------
//  gives_check: does move m (from side-to-move) put the opponent king in check?
// ---------------------------------------------------------------------------
inline bool Position::gives_check(Move m) const {
    Color us = side, them = ~side;
    int from = from_sq(m), to = to_sq(m);
    MoveType mt = type_of(m);
    int oksq = king_sq(them);
    Bitboard occ = pieces();

    // Build resulting occupancy and figure the piece landing on 'to'.
    PieceType pt = ptype_of(mailbox[from]);
    Bitboard newOcc = occ ^ sq_bb(from);
    int landSq = to;
    PieceType landPt = pt;

    if (mt == MT_CASTLE) {
        bool kingside = to > from;
        int rto = kingside ? (to - 1) : (to + 1);
        Bitboard co = (occ ^ sq_bb(from)) | sq_bb(to);
        int rfrom = kingside ? (us==WHITE?7:63) : (us==WHITE?0:56);
        co ^= sq_bb(rfrom); co |= sq_bb(rto);
        return (rook_attacks(rto, co) & sq_bb(oksq)) != 0;
    }
    if (mt == MT_ENPASSANT) {
        int capSq = (us == WHITE) ? to - 8 : to + 8;
        newOcc = (occ ^ sq_bb(from) ^ sq_bb(capSq)) | sq_bb(to);
        // direct pawn check + any discovered
        if (PawnAttacks[us][to] & sq_bb(oksq)) return true;
        if (rook_attacks(oksq, newOcc) & pieces(us, ROOK, QUEEN)) return true;
        if (bishop_attacks(oksq, newOcc) & pieces(us, BISHOP, QUEEN)) return true;
        return false;
    }
    if (mt == MT_PROMO) { landPt = promo_of(m); }
    newOcc |= sq_bb(to);   // 'to' is occupied by our (possibly promoted) piece afterwards

    // Direct check from landing piece
    bool direct = false;
    switch (landPt) {
        case PAWN:   direct = PawnAttacks[us][landSq] & sq_bb(oksq); break;
        case KNIGHT: direct = KnightAttacks[landSq] & sq_bb(oksq); break;
        case BISHOP: direct = bishop_attacks(landSq, newOcc) & sq_bb(oksq); break;
        case ROOK:   direct = rook_attacks(landSq, newOcc) & sq_bb(oksq); break;
        case QUEEN:  direct = queen_attacks(landSq, newOcc) & sq_bb(oksq); break;
        default: break;
    }
    if (direct) return true;
    // Discovered check: a slider of ours now sees the king through vacated 'from'.
    if (rook_attacks(oksq, newOcc) & pieces(us, ROOK, QUEEN) & ~sq_bb(landSq)) return true;
    if (bishop_attacks(oksq, newOcc) & pieces(us, BISHOP, QUEEN) & ~sq_bb(landSq)) return true;
    return false;
}

// ---------------------------------------------------------------------------
//  Static Exchange Evaluation (>= threshold?)
// ---------------------------------------------------------------------------
inline bool Position::see_ge(Move m, int threshold) const {
    MoveType mt = type_of(m);
    if (mt == MT_CASTLE) return 0 >= threshold;   // castling never captures
    if (mt == MT_PROMO)  return true;             // promotions are searched; never SEE-pruned

    int from = from_sq(m), to = to_sq(m);
    Bitboard occ = pieces() ^ sq_bb(from) ^ sq_bb(to);
    int captured = (mt == MT_ENPASSANT) ? SeeValue[PAWN]
                                        : (mailbox[to] == NO_PIECE ? 0 : SeeValue[ptype_of(mailbox[to])]);
    int swap = captured - threshold;
    if (swap < 0) return false;                   // even winning the target isn't enough

    swap = SeeValue[ptype_of(mailbox[from])] - swap;
    if (swap <= 0) return true;                    // recapturing our piece still meets threshold

    if (mt == MT_ENPASSANT) occ ^= sq_bb((side == WHITE) ? to - 8 : to + 8);

    Bitboard bishopsQ = byType[BISHOP] | byType[QUEEN];
    Bitboard rooksQ   = byType[ROOK]   | byType[QUEEN];
    Bitboard attackers = attackers_to(to, occ);
    Color stm = side;      // side that just captured on 'to'
    int res = 1;
    Bitboard bb;

    while (true) {
        stm = ~stm;
        attackers &= occ;
        Bitboard stmAtt = attackers & byColor[stm];
        if (!stmAtt) break;
        res ^= 1;

        if ((bb = stmAtt & byType[PAWN])) {
            if ((swap = SeeValue[PAWN] - swap) < res) break;
            occ ^= (bb & -bb);
            attackers |= bishop_attacks(to, occ) & bishopsQ;
        } else if ((bb = stmAtt & byType[KNIGHT])) {
            if ((swap = SeeValue[KNIGHT] - swap) < res) break;
            occ ^= (bb & -bb);
        } else if ((bb = stmAtt & byType[BISHOP])) {
            if ((swap = SeeValue[BISHOP] - swap) < res) break;
            occ ^= (bb & -bb);
            attackers |= bishop_attacks(to, occ) & bishopsQ;
        } else if ((bb = stmAtt & byType[ROOK])) {
            if ((swap = SeeValue[ROOK] - swap) < res) break;
            occ ^= (bb & -bb);
            attackers |= rook_attacks(to, occ) & rooksQ;
        } else if ((bb = stmAtt & byType[QUEEN])) {
            if ((swap = SeeValue[QUEEN] - swap) < res) break;
            occ ^= (bb & -bb);
            attackers |= (bishop_attacks(to, occ) & bishopsQ) | (rook_attacks(to, occ) & rooksQ);
        } else { // king: can only capture if opponent has no more attackers
            return (attackers & byColor[~stm]) ? bool(res ^ 1) : bool(res);
        }
    }
    return bool(res);
}

// ---------------------------------------------------------------------------
//  Misc helpers
// ---------------------------------------------------------------------------
inline std::string move_to_uci(Move m) {
    if (m == MOVE_NONE || m == MOVE_NULL) return "0000";
    std::string s = sq_to_str(from_sq(m)) + sq_to_str(to_sq(m));
    if (type_of(m) == MT_PROMO) {
        const char* p = "nbrq";
        s += p[(m >> 12) & 3];
    }
    return s;
}

inline void Position::print() const {
    const char* L = "PNBRQKpnbrqk";
    std::string out = "\n +---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            Piece pc = mailbox[make_sq(f, r)];
            out += " | ";
            out += (pc == NO_PIECE) ? ' ' : L[pc];
        }
        out += " |\n +---+---+---+---+---+---+---+---+\n";
    }
    out += "\nFEN: " + fen() + "\n";
    fprintf(stderr, "%s", out.c_str());
}
