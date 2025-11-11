#ifndef AWALE_H
#define AWALE_H

#include <stdbool.h>
#include <stddef.h>

#define HOUSES_PER_SIDE 6

// Board representation: 12 houses, 0-5 = player 0 left->right, 6-11 = player 1 left->right
typedef struct
{
    int board[HOUSES_PER_SIDE * 2];
    int score[2];
    int current_player; // 0 or 1
} Awale;

void awale_init(Awale *g);
void awale_print(const Awale *g);
bool awale_move(Awale *g, int house_index); // house_index is 0-5 relative to current player
bool awale_is_game_over(const Awale *g);

// Affiche le plateau selon le point de vue du joueur (POV: 0=J1, 1=J2)
// Écrit le texte dans 'out' (taille out_sz)
// Affiche le plateau selon le point de vue du joueur (POV: 0=J1, 1=J2)
// out : buffer de sortie
// current_player : indice du joueur dont c'est le tour (0 ou 1)
// current_POV : POV du destinataire (0 ou 1)
// pov_name / other_name : pseudos (NUL-terminated)
void afficher_interface_jeu(char *out, size_t out_sz,
                            const int board[12], const int scores[2],
                            int current_player, int current_POV,
                            const char *pov_name, const char *other_name);

#endif // AWALE_H
