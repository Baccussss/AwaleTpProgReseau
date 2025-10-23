#include "awale.h"
#include <stdio.h>

void awale_init(Awale *g)
{
    for (int i = 0; i < HOUSES_PER_SIDE * 2; ++i)
    {
        g->board[i] = 4; // 4 graines par maison
    }
    g->score[0] = g->score[1] = 0;
    g->current_player = 0;
}

static int absolute_index(int player, int house)
{
    // player 0 houses 0..5 map to 0..5; player1 houses 0..5 map to 6..11
    return (player == 0) ? house : (HOUSES_PER_SIDE + house);
}

void awale_print(const Awale *g)
{
    // Print board: top row player1 houses (11..6), bottom row player0 houses (0..5)
    printf("\n    ");
    for (int i = HOUSES_PER_SIDE - 1; i >= 0; --i)
    {
        printf(" %2d ", g->board[HOUSES_PER_SIDE + i]);
    }
    printf("\n");
    printf("%2d ", g->score[1]);
    for (int i = 0; i < HOUSES_PER_SIDE * 3; ++i)
        printf(" ");
    printf(" %2d\n", g->score[0]);
    printf("    ");
    for (int i = 0; i < HOUSES_PER_SIDE; ++i)
    {
        printf(" %2d ", g->board[i]);
    }
    printf("\n\n");
}

bool awale_is_game_over(const Awale *g)
{
    int total = g->score[0] + g->score[1];
    if (total >= 48)
        return true; // all seeds captured
    // Or if one side has no seeds at start of turn -> game may end
    int sum0 = 0, sum1 = 0;
    for (int i = 0; i < HOUSES_PER_SIDE; ++i)
    {
        sum0 += g->board[i];
        sum1 += g->board[HOUSES_PER_SIDE + i];
    }
    if (sum0 == 0 || sum1 == 0)
        return true;
    return false;
}

// sowing and capture rules simplified: sow counter-clockwise skipping starting house, capture when last seed lands in opponent house with 2 or 3 seeds (after landing), and chain captures backwards.

bool awale_move(Awale *g, int house_index)
{
    if (house_index < 0 || house_index >= HOUSES_PER_SIDE)
        return false;
    int start = absolute_index(g->current_player, house_index);
    int seeds = g->board[start];
    if (seeds == 0)
        return false;
    g->board[start] = 0;

    int pos = start;
    while (seeds > 0)
    {
        pos = (pos + 1) % (HOUSES_PER_SIDE * 2);
        if (pos == start)
            continue; // skip originating house
        g->board[pos] += 1;
        seeds--;
    }

    // capture phase
    int captured = 0;
    // if last landed in opponent side
    int opponent_offset = (g->current_player == 0) ? HOUSES_PER_SIDE : 0;
    while (pos >= opponent_offset && pos < opponent_offset + HOUSES_PER_SIDE)
    {
        if (g->board[pos] == 2 || g->board[pos] == 3)
        {
            captured += g->board[pos];
            g->board[pos] = 0;
            pos = (pos - 1 + HOUSES_PER_SIDE * 2) % (HOUSES_PER_SIDE * 2);
        }
        else
            break;
    }
    g->score[g->current_player] += captured;

    // next player's turn only if move wasn't starve opponent? For simplicity, always switch.
    g->current_player = 1 - g->current_player;
    return true;
}
