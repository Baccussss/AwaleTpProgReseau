#ifndef AWALE_H
#define AWALE_H

#include <stdbool.h>

#define HOUSES_PER_SIDE 6

// Board representation: 12 houses, 0-5 = player 0 left->right, 6-11 = player 1 left->right
typedef struct {
    int board[HOUSES_PER_SIDE * 2];
    int score[2];
    int current_player; // 0 or 1
} Awale;

void awale_init(Awale *g);
void awale_print(const Awale *g);
bool awale_move(Awale *g, int house_index); // house_index is 0-5 relative to current player
bool awale_is_game_over(const Awale *g);

#endif // AWALE_H
