#include <stdio.h>
#include <stdlib.h>
#include "awale.h"

int main(void) {
    Awale g;
    awale_init(&g);
    char line[100];

    while (!awale_is_game_over(&g)) {
        awale_print(&g);
        printf("Joueur %d, choisissez une maison (0-%d) : ", g.current_player + 1, HOUSES_PER_SIDE - 1);
        if (!fgets(line, sizeof(line), stdin)) break;
        int h = atoi(line);
        if (!awale_move(&g, h)) {
            printf("Coup invalide, réessayez.\n");
        }
    }
    awale_print(&g);
    printf("Partie terminée. Scores : Joueur 1 = %d, Joueur 2 = %d\n", g.score[0], g.score[1]);
    if (g.score[0] > g.score[1]) printf("Joueur 1 gagne!\n");
    else if (g.score[1] > g.score[0]) printf("Joueur 2 gagne!\n");
    else printf("Egalité!\n");
    return 0;
}
