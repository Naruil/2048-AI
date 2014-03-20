#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <sys/time.h>
#include <time.h>

#define AUTO

#define SIZE (4)
typedef int board_t[SIZE][SIZE];
struct hash_elem {
    int hash;
    int depth;
    double value;
};

enum input {
    LEFT = 0,
    RIGHT = 1,
    UP = 2,
    DOWN = 3,
    QUIT = 4
};

/* Static mappings & initialzation ***********************************/

/* Weight of each value */
int value_weight[16];
/* Mapping from value to power of 2 form */
int value_real[16];
/* Default search depth for each #zero-blocks */
int depth_map[] = {6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4};

/* Weight map of cells */
static const board_t cell_weight = {
    {17, 13, 11, 10},
    {13, 10,  9,  9},
    {11,  9,  8,  8},
    {10,  9,  8,  8}
};

/* Used for hash table */
static const board_t primes = {
    {22189, 28813, 37633, 43201}, 
    {47629, 60493, 63949, 65713}, 
    {69313, 73009, 76801, 84673}, 
    {106033, 108301, 112909, 115249}
};
    
void init() {
    int i;
    int cur_weight = 1;
    int cur_real = 2;
    for (i = 1; i < 16; i++) {
        value_weight[i] = cur_weight;
        value_real[i] = cur_real;
        cur_weight *= 3;
        cur_real *= 2;
    }
}

/* Util Functions *****************************************************/
long gettime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 * 1000 + tv.tv_usec;
}

void draw_grid(int y, int x) {
    mvprintw(y++, x,    "#####################################");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#-----------------------------------#");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#-----------------------------------#");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#-----------------------------------#");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#        |        |        |        #");
    mvprintw(y++, x,    "#####################################");
    mvprintw(y+2, x,    "Control: wasd  Exit: q");
}


void board_dump(board_t b, int y, int x) { 
    int i, j;
    draw_grid(y, x);
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            mvprintw(i * 4 + 2 + y, j * 9 + 3 + x, "%d", value_real[b[i][j]]);
        }
    }
}

int board_count_zero(board_t b) {
    int cnt = 0;
    int i, j;
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            if (b[i][j] == 0)
                cnt++;
        }
    }
    return cnt;
}

void board_clear(board_t b) {
    int i, j;
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            b[i][j] = 0;
        }
    }
}

int board_hash(board_t b) {
    int i, j;
    int hash = 0;
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            hash += b[i][j] * primes[i][j];
        }
    }
    return hash;
}

void board_rnd_gen_cell(board_t b) {
    int i, j;
    int cnt = board_count_zero(b);
    int gen = random() % cnt;
    int n = (random() % 10) == 0 ? 2 : 1;

    cnt = 0;    

    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            if (b[i][j] == 0) {
                if (cnt == gen) {
                    b[i][j] = n;
                    return;
                }
                cnt++;
            }
        }
    }
}

void delay() {
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = 10000000;
    nanosleep(&t, NULL);
}

/* Performance statistic */
long stat_time[16];
long stat_count[16]; 
void stat(int depth, long time) {
    stat_count[depth]++;
    stat_time[depth] += time;
}

void stat_dump() {
    int i;
    int line = 0;
    mvprintw(25 + line++, 8, "Performance Stat");
    for (i = 0; i < 16; i++) {
        if (!stat_count[i])
            continue;
        mvprintw(25 + line++, 8, "[Depth %d] %ld us * %d times",
            i, stat_time[i] / stat_count[i], stat_count[i]);
    }   
}

/* Game logic: Move to a direction **************************************************/

/* Return score earned, return 1 if moved with zero score */
#define movefunc(src_cell, combine_cell, nocombine_cell)\
{\
    int i, j = 0;\
    int moved = 0;\
    int score = 0;\
    for (i = 0; i < SIZE; i++) {\
        int last = 0;\
        int j2 = 0;\
        for (j = 0; j < SIZE; j++) {\
            int v = src_cell;\
            if (v == 0) {\
                continue;\
            }\
            if (v == last) {\
                last = 0;\
                combine_cell = v + 1;\
                score += value_real[v + 1];\
            } else {\
                if (j2 < j)\
                    moved = 1;\
                last = v;\
                nocombine_cell = v;\
                j2++;\
            }\
        }\
    }\
    return score ? score : moved;\
}

#define REVERSE(i) (SIZE - 1 - (i))



int move_left(board_t src, board_t dst) {
    movefunc(src[i][j], dst[i][j2 - 1], dst[i][j2]);
}

int move_right(board_t src, board_t dst) {
    movefunc(src[i][REVERSE(j)], dst[i][REVERSE(j2 - 1)], dst[i][REVERSE(j2)]);
}

int move_up(board_t src, board_t dst) {
    movefunc(src[j][i], dst[j2 - 1][i], dst[j2][i]);
}

int move_down(board_t src, board_t dst) {
    movefunc(src[REVERSE(j)][i], dst[REVERSE(j2 - 1)][i], dst[REVERSE(j2)][i]);
}


/* AI-related functions **************************************************/
double value(board_t b, int depth, int *choice, double max);

/* Immediate value score estimation for a board */
int imm_value(board_t b) {
    int i, j;
    int result = 0;

    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            result += value_weight[b[i][j]] * cell_weight[i][j];
        }
    }
    return result;
}

#define HASH_SIZE (35317) 
struct hash_elem vcache[HASH_SIZE];
void cache_board_value(board_t b, int depth, double value) {
    int hash = board_hash(b);
    int index = hash % HASH_SIZE;
    vcache[index].hash = hash;
    vcache[index].value = value;
    vcache[index].depth = depth;
}

int qcnt;
int qmiss;
double query_board_value(board_t b, int depth) {
    int hash = board_hash(b);
    int index = hash % HASH_SIZE;
    qcnt++;
    if (vcache[index].hash == hash && vcache[index].depth >= depth) {
        return vcache[index].value;
    }
    qmiss++;
    return -1;
}

/* Generate 2/4 at every posible position, return the average value score 
 * b        : the board
 * depth    : depth of the recursive search
 * max      : current maximum value score
 * sampled  : sample rate, 0 means no sample
 */
double rnd_value(board_t b, int depth, double max, int sampled) {
    int i, j;
    int cnt = 0;
    double sum = 0;
    static int scnt = 0;

    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            /* Check zero */
            if (b[i][j])
                continue;

            /* Do sampling to reduce computation if needed */
            if (sampled) {
                scnt++;
                if(scnt % sampled)
                    continue;
            }
            cnt += 9;
            b[i][j] = 1;
            sum += 9 * value(b, depth, NULL, max);
            /* We do not take random 4 into consideration as it is rare */
            if (depth >= 5) {
                cnt += 1;
                b[i][j] = 2;
                sum += value(b, depth, NULL, max);
            }
            /**/
            b[i][j] = 0;
        }
    }
    return sum / cnt;
}

/* Return the value score for given board, zero score means died 
 * b        : the board
 * depth    : depth of the recursive search
 * choice   : used to return the final choise of movement
 * max      : current maximum value score
 */
double value(board_t b, int depth, int *choice, double max) {
    /* Estimate the value score */
    int estimate = imm_value(b);

    /* Decrease depth if estimation is too low */
    if (estimate < max * 0.7)
        depth--;

    /* Return estimation at level 0 */
    if (depth <= 0)
        return estimate;

    /* Adjust next depth according to depth_map */ 
    int next_depth = depth - 1;    
    if (depth > 3) {
        int zeros = board_count_zero(b);
        if (next_depth > depth_map[zeros])
            next_depth--; 
    } 
    
    int i;
    int moved[4];
    double maxv = 0;
    board_t tmp[4] = {0};
    int my_choice = QUIT; /* Default choice */

    if (!choice) {
        double v = query_board_value(b, depth);
        if (v >= 0)
            return v;
    }
    
    moved[LEFT] = move_left(b, tmp[LEFT]);
    moved[RIGHT] = move_right(b, tmp[RIGHT]);
    moved[UP] = move_up(b, tmp[UP]);
    moved[DOWN] = move_down(b, tmp[DOWN]);

    /* Estimate the maximum value score */
    if (depth > 2)
    for (i = 0; i < 4; i++) {
        int v = imm_value(tmp[0]);
        max = v > max ? v : max;
    }   
    
    /* Try all the four direction */ 
    for (i = 0; i < 4; i++) {
        int c;
        if (!moved[i])
            continue;
        int sample = 0; //depth < 3 ? 3 : 1;
        double v = rnd_value(tmp[i], next_depth, max, sample);
        if (v > maxv) {
            my_choice = i;
            maxv = v;
            max = maxv;
        }
    }

    if (choice)
        *choice = my_choice;
   
    cache_board_value(b, depth, maxv); 
    return maxv;
}

/* Game logic: Control and Interface *************************************/
static int get_AI_input(board_t b) {
    int choice;
    int zeros = board_count_zero(b);

    long start = gettime();
    double v = value(b, depth_map[zeros], &choice, 0);
    long timeval = gettime() - start;
    stat(depth_map[zeros], timeval);

    return choice;
}

static int get_keyboard_input() {
    char c;
    while(1) {
        //c = getchar();
        c = getch();
        switch(c) {
            case 'w': return UP;
            case 'a': return LEFT;
            case 's': return DOWN;
            case 'd': return RIGHT;
            case 'q': return QUIT;
        }
    }
}

int auto_play = 0;
int suggestion = 0;

void game_loop() {

    board_t a = {0};
    board_t b = {0};
    board_t *cur;
    board_t *next;

    int input;
    int AI_input;
    int score = 0;
    
    cur = &a;
    next = &b;

    board_rnd_gen_cell(*cur);
    
    while (1) {
        clear();
        
        /* Draw the board */
        board_dump(*cur, 4, 8);
        // stat_dump(); 
        
        /* AI computation */
        if (auto_play || suggestion) {
            AI_input = get_AI_input(*cur);
            const char *move_text[] = {"Left", "Right", "Up", "Down", "Game Over"};
            mvprintw(1, 8, "Suggest: %s", move_text[AI_input]);
        }
        mvprintw(2, 8, "Score: %d", score);
        
        /* Update screen */
        refresh();
        
        /* Get input */
        if (auto_play) {
            input = AI_input;
        } else {
            input = get_keyboard_input();
        }

        int moved = 0;
        switch(input) {
            case UP:
                moved = move_up(*cur, *next); break;
            case LEFT:
                moved = move_left(*cur, *next); break;
            case DOWN:
                moved = move_down(*cur, *next); break;
            case RIGHT:
                moved = move_right(*cur, *next); break;
            case QUIT:
                return;
            default:
                continue;
        }

        if (!moved)
            continue;
       
        if (moved != 1)
            score += moved;
 
        /* Generate new cell */
        board_rnd_gen_cell(*next);

        /* Switch cur and next */ 
        board_t *temp = cur;
        cur = next;
        next = temp; 
        board_clear(*next);
    }

}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "as")) != -1) {
        switch (opt) {
            case 'a':
                auto_play = 1;
                break;
            case 's':
                suggestion = 1;
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s [-a] [-s]\r\n", argv[0]);
                fprintf(stderr, "-a:  Let AI play the game\r\n");
                fprintf(stderr, "-s:  Display AI suggestion\r\n");
                exit(EXIT_FAILURE);
        }

    }


    init();
    srandom(time(NULL)); 
    initscr();
    noecho();
    
    game_loop();
    
    refresh();
    endwin();
    return 0;
}

