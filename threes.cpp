#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "threes.h"

// advance///

#include "config.h"
#if defined(HAVE_UNORDERED_MAP)
    #include <unordered_map>
    typedef std::unordered_map<board_t, float> trans_table_t;
#elif defined(HAVE_TR1_UNORDERED_MAP)
    #include <tr1/unordered_map>
    typedef std::tr1::unordered_map<board_t, float> trans_table_t;
#else
    #include <map>
    typedef std::map<board_t, float> trans_table_t;
#endif

/* MSVC compatibility: undefine max and min macros */
#if defined(max)
#undef max
#endif

#if defined(min)
#undef min
#endif

// Transpose rows/columns in a board:
//   0123       0426       048c
//   4567  -->  1537  -->  159d
//   89ab       8cae       26ae
//   cdef       9dbf       37bf
static inline board_t transpose(board_t x)
{
	board_t a1 = x & 0xF0F00F0FF0F00F0FULL;
	board_t a2 = x & 0x0000F0F00000F0F0ULL;
	board_t a3 = x & 0x0F0F00000F0F0000ULL;
	board_t a = a1 | (a2 << 12) | (a3 >> 12);
	board_t b1 = a & 0xFF00FF0000FF00FFULL;
	board_t b2 = a & 0x00FF00FF00000000ULL;
	board_t b3 = a & 0x00000000FF00FF00ULL;
	return b1 | (b2 >> 24) | (b3 << 24);
}

/* We can perform state lookups one row at a time by using arrays with 65536 entries. */

/* Move tables. Each row or compressed column is mapped to (oldrow^newrow) assuming row/col 0.
 *
 * Thus, the value is 0 if there is no move, and otherwise equals a value that can easily be
 * xor'ed into the current board state to update the board. */

// TODO understand the semantic meaning of each table.
static row_t row_left_table[65536];
static row_t row_right_table[65536];
static board_t col_up_table[65536];
static board_t col_down_table[65536];
static char row_max_table[65536];
static float heur_score_table[65536];
static float score_table[65536];

// Heuristic scoring settings
static float SCORE_LOST_PENALTY = 200000.0f;
static float SCORE_MONOTONICITY_POWER = 4.0f;
static float SCORE_MONOTONICITY_WEIGHT = 47.0f;
static float SCORE_SUM_POWER = 0.0f;
static float SCORE_SUM_WEIGHT = 0.0f;
static float SCORE_MERGES_WEIGHT = 0.0f;
static float SCORE_12_MERGES_WEIGHT = 0.0f;
static float SCORE_EMPTY_WEIGHT = 0.0f;

void set_heurweights(float *f, int flen) {
    if(flen != 7) {
        fprintf(stderr, "Incorrect number of arguments to set_heurweights: got %d\n", flen);
        exit(-1);
    }
    SCORE_MONOTONICITY_POWER = f[0];
    SCORE_MONOTONICITY_WEIGHT = f[1];
    SCORE_SUM_POWER = f[2];
    SCORE_SUM_WEIGHT = f[3];
    SCORE_MERGES_WEIGHT = f[4];
    SCORE_12_MERGES_WEIGHT = f[5];
    SCORE_EMPTY_WEIGHT = f[6];
}

void init_tables() {
    for(unsigned row = 0; row < 65536; ++row) {
        unsigned line[4] = {
                (row >>  0) & 0xf,
                (row >>  4) & 0xf,
                (row >>  8) & 0xf,
                (row >> 12) & 0xf
        };

        // Score
        float score = 0.0f;
        for (int i = 0; i < 4; ++i) {
            int rank = line[i];
            if (rank >= 3) {
                score += powf(3, rank-2);
            }
        }
        score_table[row] = score;
        row_max_table[row] = std::max(std::max(line[0], line[1]), std::max(line[2], line[3]));

        // Heuristic score
        float sum = 0;    // TODO the semantic meaning of sum
        int empty = 0;
        int merges = 0;
        int onetwo_merges = 0;
        int prev = 0;
        int counter = 0;

        // compute merge and empty
        for (int i = 0; i < 4; ++i) {
            int rank = line[i];
            sum += pow(rank, SCORE_SUM_POWER);
            if (rank == 0) {
                empty++;
            } else {
                if (prev == rank) {
                    counter++;
                } else if (counter > 0) {
                    merges += 1 + counter;
                    counter = 0;
                }
                prev = rank;
            }
        }
        if (counter > 0) {
            merges += 1 + counter;
        }

        // compute one_two_merge
        for (int i = 1; i < 4; ++i) {
            if ((line[i-1] == 1 && line[i] == 2) || (line[i-1] == 2 && line[i] == 1)) {
                onetwo_merges++;
            }
        }

        float monotonicity_left = 0;
        float monotonicity_right = 0;
        for (int i = 1; i < 4; ++i) {
            if (line[i-1] > line[i]) {
                monotonicity_left += pow(line[i-1], SCORE_MONOTONICITY_POWER) - pow(line[i], SCORE_MONOTONICITY_POWER);
            } else {
                monotonicity_right += pow(line[i], SCORE_MONOTONICITY_POWER) - pow(line[i-1], SCORE_MONOTONICITY_POWER);
            }
        }

        heur_score_table[row] = SCORE_LOST_PENALTY
            + SCORE_EMPTY_WEIGHT * empty
            + SCORE_MERGES_WEIGHT * merges
            + SCORE_12_MERGES_WEIGHT * onetwo_merges
            - SCORE_MONOTONICITY_WEIGHT * std::min(monotonicity_left, monotonicity_right)
            - SCORE_MONOTONICITY_WEIGHT *(monotonicity_left+ monotonicity_right)
            - SCORE_SUM_WEIGHT * sum;

        // execute a move to the left
        int i;
 
        for(i=0; i<3; i++) {
            if(line[i] == 0) {
                line[i] = line[i+1];
                break;
            } else if(line[i] == 1 && line[i+1] == 2) {
                line[i] = 3;
                break;
            } else if(line[i] == 2 && line[i+1] == 1) {
                line[i] = 3;
                break;
            } else if(line[i] == line[i+1] && line[i] >= 3) {
                if(line[i] != 15) {
                    /* Pretend that 12288 + 12288 = 12288 */
                    line[i]++;
                }
                break;
            }
        }

        // no merge happens
        if(i == 3) continue;

        // move the remaining tils to the left.
        for(int j=i+1; j<3; j++)
            line[j] = line[j+1];
        line[3] = 0;

        row_t result = (line[0] <<  0) |
                       (line[1] <<  4) |
                       (line[2] <<  8) |
                       (line[3] << 12);
        row_t rev_result = reverse_row(result);
        // FIXME unsigned might be wrong.
        unsigned rev_row = reverse_row(row);

        row_left_table [    row] =                row  ^                result;
        row_right_table[rev_row] =            rev_row  ^            rev_result;
        col_up_table   [    row] = unpack_col(    row) ^ unpack_col(    result);
        col_down_table [rev_row] = unpack_col(rev_row) ^ unpack_col(rev_result);
    }
}

#define DO_LINE(tbl,i,lookup,xv) do { \
        tmp = tbl[lookup]; \
        if(tmp) { \
            ch += 256 + (1 << i); \
            ret ^= xv; \
        } \
    } while(0)

#define DO_ROW(tbl,i) DO_LINE(tbl,i, (board >> (16*i)) & ROW_MASK,          tmp << (16*i))
#define DO_COL(tbl,i) DO_LINE(tbl,i, pack_col((board >> (4*i)) & COL_MASK), tmp << (4*i))

static inline board_t execute_move_0(board_t board, int *changed) {
    int ch = 0;
    board_t tmp;
    board_t ret = board;

    DO_COL(col_up_table, 0);
    DO_COL(col_up_table, 1);
    DO_COL(col_up_table, 2);
    DO_COL(col_up_table, 3);

    *changed = ch;
    return ret;
}

static inline board_t execute_move_1(board_t board, int *changed) {
    int ch = 0;
    board_t tmp;
    board_t ret = board;

    DO_COL(col_down_table, 0);
    DO_COL(col_down_table, 1);
    DO_COL(col_down_table, 2);
    DO_COL(col_down_table, 3);

    *changed = ch;
    return ret;
}

static inline board_t execute_move_2(board_t board, int *changed) {
    int ch = 0;
    board_t tmp;
    board_t ret = board;

    DO_ROW(row_left_table, 0);
    DO_ROW(row_left_table, 1);
    DO_ROW(row_left_table, 2);
    DO_ROW(row_left_table, 3);

    *changed = ch;
    return ret;
}

static inline board_t execute_move_3(board_t board, int *changed) {
    int ch = 0;
    board_t tmp;
    board_t ret = board;

    DO_ROW(row_right_table, 0);
    DO_ROW(row_right_table, 1);
    DO_ROW(row_right_table, 2);
    DO_ROW(row_right_table, 3);

    *changed = ch;
    return ret;
}
#undef DO_ROW
#undef DO_COL
#undef DO_LINE

/*
 * Execute a move. Store status about the move's effects in `changed'.
 * The format of `changed' is (num_changed << 8) | changed_bits
 * where the ith bit of changed_bits is set iff row/col i moved.
 */
static inline board_t execute_move(int move, board_t board, int *changed) {
    switch(move) {
    case 0: // up
        return execute_move_0(board, changed);
    case 1: // down
        return execute_move_1(board, changed);
    case 2: // left
        return execute_move_2(board, changed);
    case 3: // right
        return execute_move_3(board, changed);
    default:
        // should not happen
        *changed = 0;
        return ~0ULL;
    }
}

static inline int get_row_max_rank(row_t row) {
    return row_max_table[row];
}

static inline int get_max_rank(board_t board) {
    int maxrank = 0;
    while (board) {
        maxrank = std::max(maxrank, get_row_max_rank(board & ROW_MASK));
        board >>= 16;
    }
    return maxrank;
}

static inline int count_distinct_tiles(board_t board) {
    uint16_t bitset = 0;
    while (board) {
        bitset |= 1<<(board & 0xf);
        board >>= 4;
    }

    // Don't count empty tiles or "1","2" tiles.
    bitset >>= 3;

    int count = 0;
    while (bitset) {
        bitset &= bitset - 1;
        count++;
    }
    return count;
}

/* Place a new tile on the board. Assumes the board is empty at the target location. */
static inline board_t insert_tile(int move, board_t board, int pos, int tile) {
    switch(move) {
    case 0: // up
        return board | (((board_t)tile) << (pos*4 + 48));
    case 1: // down
        return board | (((board_t)tile) << pos*4);
    case 2: // left
        return board | (((board_t)tile) << (12 + pos*16));
    case 3: // right
        return board | (((board_t)tile) << (pos*16));
    default:
        return ~0ULL;
    }
}

/* Optimizing the game */
// TODO the semantic meaning of trans_table.
struct eval_state {
    trans_table_t trans_table; // transposition table, to cache previously-seen moves
    int maxdepth;
    int curdepth;
    int cachehits;
    unsigned long moves_evaled;
    int depth_limit;

    eval_state() : maxdepth(0), curdepth(0), cachehits(0), moves_evaled(0), depth_limit(0) {
    }
};

// score a single board heuristically
static float score_heur_board(board_t board);
// score a single board actually
static float score_board(board_t board);
// score over all possible moves
static float score_move_node(eval_state &state, board_t board, deck_t deck, float cprob);
// score over all possible tile choices
static float score_tilechoose_node(eval_state &state, board_t board, deck_t deck, float cprob, int move, int changed);
// score over all possible tile placements
static float score_tileinsert_node(eval_state &state, board_t board, deck_t deck, float cprob, int move, int changed, int tile);

static float score_helper(board_t board, const float* table) {
    return table[(board >>  0) & ROW_MASK] +
           table[(board >> 16) & ROW_MASK] +
           table[(board >> 32) & ROW_MASK] +
           table[(board >> 48) & ROW_MASK];
}

static float score_heur_board(board_t board) {
    return score_helper(          board , heur_score_table) +
           score_helper(transpose(board), heur_score_table);
}

static float score_board(board_t board) {
    return score_helper(board, score_table);
}

static float score_tileinsert_node(eval_state &state, board_t board, deck_t deck, float cprob, int move, int changed, int tile) {
    float res = 0;
    float factor = 1.0f / (changed >> 8);
    int pos;
    cprob *= factor;
    for(pos=0; pos<4; pos++) {
        if(changed & (1<<pos))
            res += score_move_node(state, insert_tile(move, board, pos, tile), deck, cprob);
    }

    return res * factor;
}

static float score_tilechoose_node(eval_state &state, board_t board, deck_t deck, float cprob, int move, int changed) {
    float res = 0;
    int mv = DECK_MAXVAL(deck);

    if(!(deck & 0x00ffffff))
        deck = DECK_WITH_MAXVAL(INITIAL_DECK, mv);

    int a = DECK_1(deck);
    int b = DECK_2(deck);
    int c = DECK_3(deck);
    float div = a+b+c;
    float hres = 0;

    if(mv >= 7) {
        /* High tile */
        int choices = mv - 6;
        int i;

        for(i=0; i<choices; i++) {
            hres += score_tileinsert_node(state, board, deck, cprob / choices / HIGH_CARD_FREQ, move, changed, i+4);
        }
        hres /= (choices * HIGH_CARD_FREQ);

        div *= ((float)HIGH_CARD_FREQ)/(HIGH_CARD_FREQ - 1);
    }

    if(a)
        res += score_tileinsert_node(state, board, DECK_SUB_1(deck), cprob / div * a, move, changed, 1) * a;
    if(b)
        res += score_tileinsert_node(state, board, DECK_SUB_2(deck), cprob / div * b, move, changed, 2) * b;
    if(c)
        res += score_tileinsert_node(state, board, DECK_SUB_3(deck), cprob / div * c, move, changed, 3) * c;

    res /= div;
    res += hres;

    return res;
}

/* Statistics and controls */
// cprob: cumulative probability
// don't recurse into a node with a cprob less than this threshold
static const float CPROB_THRESH_BASE = 0.0001f;
static const int CACHE_DEPTH_LIMIT  = 6;

static float score_move_node(eval_state &state, board_t board, deck_t deck, float cprob) {
    if(cprob < CPROB_THRESH_BASE || state.curdepth >= state.depth_limit) {
        state.maxdepth = std::max(state.curdepth, state.maxdepth);
        return score_heur_board(board);
    }

    if(state.curdepth < CACHE_DEPTH_LIMIT) {
        const trans_table_t::iterator &i = state.trans_table.find(board);
        if(i != state.trans_table.end()) {
            state.cachehits++;
            return i->second;
        }
    }

    float best = 0.0f;
    state.curdepth++;
    for(int move=0; move<4; move++) {
        int changed;
        board_t newboard = execute_move(move, board, &changed);
        state.moves_evaled++;

        if(changed) {
            best = std::max(best, score_tilechoose_node(state, newboard, deck, cprob, move, changed));
        }
    }
    state.curdepth--;

    if(state.curdepth < CACHE_DEPTH_LIMIT) {
        state.trans_table[board] = best;
    }

    return best;
}

static float _score_toplevel_move(eval_state &state, board_t board, deck_t deck, tileset_t tileset, int move) {
    int changed;
    int maxrank = get_max_rank(board);
    board_t newboard = execute_move(move, board, &changed);

    if(!changed)
        return 0;

    deck = DECK_WITH_MAXVAL(deck, maxrank);
    float result = 0;

    int tile;
    int choices = 0;
    FOREACH_TILE(tile, tileset) {
        if(tile == 1)
            result += score_tileinsert_node(state, newboard, DECK_SUB_1(deck), 1.0f, move, changed, tile);
        else if(tile == 2)
            result += score_tileinsert_node(state, newboard, DECK_SUB_2(deck), 1.0f, move, changed, tile);
        else if(tile == 3)
            result += score_tileinsert_node(state, newboard, DECK_SUB_3(deck), 1.0f, move, changed, tile);
        else
            result += score_tileinsert_node(state, newboard, deck, 1.0f, move, changed, tile);

        choices += 1;
    }
    return result / choices + 1e-6;
}

static row_t get_quadrant(board_t board, int quadrant) {
    /* Get a single quadrant of the board.
    Return: position 0 = corner, position 1 = top/bottom edge, position 2 = left/right edge, position 3 = middle:

    0123
    4567 --> 0145, 3276, cd89, feba
    89ab
    cdef
    */

    static const char quadrants[4][4] = { {0,1,4,5}, {3,2,7,6}, {12,13,8,9}, {15,14,11,10} };
    row_t ret = 0;
    for(int i=0; i<4; i++) {
        ret |= ((board >> (4*quadrants[quadrant][i])) & 0xf) << (i*4);
    }
    return ret;
}

float score_toplevel_move(board_t board, deck_t deck, tileset_t tileset, int move) {
    float res;
    struct timeval start, finish;
    double elapsed;
    eval_state state;

    state.depth_limit = 4;
    // TODO This part of code extends search depth
//    state.depth_limit = std::max(3, count_distinct_tiles(board) - 2);
//
//    /* Opposite-corners penalty */
//    int corner_disparity = 0;
//    int maxrank = get_max_rank(board);
//    for(int q=0; q<4; q++) {
//        if(get_row_max_rank(get_quadrant(board, q)) == maxrank) {
//            /* Get rank in the opposite corner */
//            corner_disparity = maxrank - get_row_max_rank(get_quadrant(board, 3-q));
//            break;
//        }
//    }
//    if(corner_disparity <= 4 && maxrank >= 9) {
//        state.depth_limit += 2;
//    }

    // TODO we don't need to calculate the computation time.
    gettimeofday(&start, NULL);
    res = _score_toplevel_move(state, board, deck, tileset, move);
    gettimeofday(&finish, NULL);

    elapsed = (finish.tv_sec - start.tv_sec);
    elapsed += (finish.tv_usec - start.tv_usec) / 1000000.0;

//     printf("Move %d: result %f: eval'd %ld moves (%d cache hits, %d cache size) in %.2f seconds (maxdepth=%d)\n", move, res,
//         state.moves_evaled, state.cachehits, (int)state.trans_table.size(), elapsed, state.maxdepth);

    return res;
}

/* Find the best move for a given board, deck and upcoming tile.
 * 
 * Note: the deck must represent the deck BEFORE the given tile was drawn.
 * This enables correct behaviour for 3+ tiles. */
int find_best_move(board_t board, deck_t deck, tileset_t tileset) {
    int move;
    float best = 0;
    int bestmove = -1;

    print_board(board);
    printf("Current scores: heur %.0f, actual %.0f\n", score_heur_board(board), score_board(board));
    printf("Next tile:");
    int t;
    FOREACH_TILE(t, tileset)
        printf(" %x", t);
    printf(" (deck=%08x)\n", deck);

    for(move=0; move<4; move++) {
        float res = score_toplevel_move(board, deck, tileset, move);

        if(res > best) {
            best = res;
            bestmove = move;
        }
    }

    return bestmove;
}

int ask_for_move(board_t board, deck_t deck, tileset_t tileset) {
    int move;
    char validstr[5];
    char *validpos = validstr;

    (void)deck;

    print_board(board);

    for(move=0; move<4; move++) {
        int changed;
        execute_move(move, board, &changed);
        if(!changed)
            continue;
        *validpos++ = "UDLR"[move];
    }
    *validpos = 0;

    // no valid move
    if(validpos == validstr)
        return -1;

    printf("Next tile:");
    int t;
    FOREACH_TILE(t, tileset)
        printf(" %x", t);
    printf("\n");

    while(1) {
        char movestr[64];
        const char *allmoves = "UDLR";

        printf("Move [%s]? ", validstr);

        if(!fgets(movestr, sizeof(movestr)-1, stdin))
            return -1;

        if(!strchr(validstr, toupper(movestr[0]))) {
            printf("Invalid move.\n");
            continue;
        }

        return strchr(allmoves, toupper(movestr[0])) - allmoves;
    }
}

/* Playing the game */
static int draw_deck(deck_t *deck) {
    int a = DECK_1(*deck);
    int b = DECK_2(*deck);
    int c = DECK_3(*deck);
    int r = unif_random(a+b+c);

    if(r < a) {
        *deck = DECK_SUB_1(*deck);
        return 1;
    } else if(r-a < b) {
        *deck = DECK_SUB_2(*deck);
        return 2;
    } else {
        *deck = DECK_SUB_3(*deck);
        return 3;
    }
}

static board_t initial_board(deck_t *deck) {
    int i;
    board_t board = 0;

    /* Draw nine initial values */
    for(i=0; i<9; i++) {
        board |= ((board_t)draw_deck(deck)) << (4*i);
    }

    /* Shuffle the board (Fisher-Yates) */
    for(i=15; i>=1; i--) {
        int j = unif_random(i+1);
        board_t exc = ((board >> (4*i)) & 0xf) ^ ((board >> (4*j)) & 0xf);
        board ^= (exc << (4*i));
        board ^= (exc << (4*j));
    }

    return board;
}

static int random_tile(tileset_t choices) {
    int count = 0;
    int t;
    FOREACH_TILE(t, choices)
        count++;

    int r = unif_random(count);
    FOREACH_TILE(t, choices) {
        if(r == 0)
            break;
        r--;
    }
    return t;
}

void play_game(get_move_func_t get_move) {
    deck_t deck = INITIAL_DECK;
    board_t board = initial_board(&deck);
    int moveno = 0;

    while(1) {
        deck_t olddeck = deck;
        int i;
        int move;
        int tile;
        tileset_t tileset;
        int changed;
        int maxrank = get_max_rank(board);

        if(!deck)
            olddeck = deck = INITIAL_DECK;

        /* TODO: find out if this is actually how the random high tiles are drawn */
        if(maxrank >= 7 && unif_random(HIGH_CARD_FREQ) == 0) {
            if(maxrank == 7) {
                tileset = 1<<4;
            } else if(maxrank == 8) {
                tileset = 3<<4;
            } else {
                tileset = 7<<(unif_random(maxrank-8)+4);
            }
        } else {
            tileset = 1<<draw_deck(&deck);
        }
        tile = random_tile(tileset);

        printf("\nMove #%d, current score=%.0f\n", ++moveno, score_board(board));

        move = get_move(board, olddeck, tileset);
        if(move < 0)
            break;

        board = execute_move(move, board, &changed);
        int count = changed >> 8;
        int choice = unif_random(count);
        for(i=0; i<4; i++) {
            if(changed & (1<<i)) {
                if(choice == 0)
                    break;
                choice--;
            }
        }

        board = insert_tile(move, board, i, tile);
    }

    print_board(board);
    printf("\nGame over. Your score is %.0f. The highest rank you achieved was %d.\n", score_board(board), get_max_rank(board));
}

int main() {
    init_tables();
    play_game(find_best_move);
    return 0;
}
