#include "awale.h"
#include <stdio.h>
#include <string.h>

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
    // maisons du joueur 0 0..5 -> indices 0..5 ; joueur 1 0..5 -> indices 6..11
    return (player == 0) ? house : (HOUSES_PER_SIDE + house);
}

void awale_print(const Awale *g)
{
    printf("\n");
    printf("          ====== PLATEAU D'AWALE ======\n\n");

    // --- Camp du joueur 2 (haut) ---
    printf("         Camp du Joueur 2 (haut)\n");
    printf("        ");
    for (int i = HOUSES_PER_SIDE - 1; i >= 0; --i)
    {
        printf(" [%2d]", g->board[HOUSES_PER_SIDE + i]);
    }
    printf("\n");
    printf("                Score J2 : %2d\n\n", g->score[1]);

    // --- Camp du joueur 1 (bas) ---
    printf("         Camp du Joueur 1 (bas)\n");
    printf("        ");
    for (int i = 0; i < HOUSES_PER_SIDE; ++i)
    {
        printf(" [%2d]", g->board[i]);
    }
    printf("\n");
    printf("                Score J1 : %2d\n\n", g->score[0]);

    // Indication du joueur courant
    printf("   --> C'est au Joueur %d de jouer <--\n\n", g->current_player + 1);
}

bool awale_is_game_over(const Awale *g)
{
    int total = g->score[0] + g->score[1];
    if (total >= 48)
        return true; // toutes les graines capturées
    // Ou si un côté n'a plus de graines au début du tour -> la partie peut se terminer
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

// Règles simplifiées de semis et de capture : semer dans le sens anti-horaire en sautant la maison de départ, capturer quand la dernière graine atterrit dans une maison adverse contenant 2 ou 3 graines (après l'atterrissage), et enchaîner les captures en remontant.

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
            continue; // sauter la maison d'origine
        g->board[pos] += 1;
        seeds--;
    }

    // phase de capture
    int captured = 0;
    // si la dernière graine atterrit dans le camp adverse
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

    // passer au joueur suivant
    g->current_player = 1 - g->current_player;
    return true;
}

void afficher_interface_jeu(char *out, size_t out_sz,
                            const int board[12], const int scores[2],
                            int current_player, int current_POV,
                            const char *pov_name, const char *other_name)
{
    int top[6], bottom[6];
    const char *top_name, *bottom_name;
    if (current_POV == 0 || current_POV == -1) // POV joueur 1 ou observateur
    {
        for (int i = 0; i < 6; ++i)
        {
            bottom[i] = board[i];   // 0..5
            top[i] = board[11 - i]; // 11..6
        }
        bottom_name = pov_name;
        top_name = other_name;
    }
    else
    {
        for (int i = 0; i < 6; ++i) // POV joueur 2
        {
            bottom[i] = board[6 + i]; // 6..11
            top[i] = board[5 - i];    // 5..0
        }
        bottom_name = pov_name;
        top_name = other_name;
    }

    char msg[256];
    if (current_POV == -1) // observateur
    {
        snprintf(msg, sizeof(msg), "C'est au tour de %s de jouer.\n", (current_player == 0) ? pov_name : other_name);
    }
    else if (current_player == current_POV)
    {
        snprintf(msg, sizeof(msg), "À vous de jouer (%s) — Choisissez une maison (0-5): JOUER <0-5>\n", pov_name);
    }
    else
    {
        snprintf(msg, sizeof(msg), "En attente du coup adverse de %s...\n", other_name);
    }

    snprintf(out, out_sz,
             "\n========== PLATEAU D'AWALE ==========%s\n\n"
             " ___ ___ ___ ___ ___ ___ \n"
             "|%2d |%2d |%2d |%2d |%2d |%2d |  [%s]\n"
             "|___|___|___|___|___|___|  [Score: %d]\n"
             "|%2d |%2d |%2d |%2d |%2d |%2d |  [%s]\n"
             "|___|___|___|___|___|___|  [Score: %d]\n\n"
             "%s",
             "",
             top[0], top[1], top[2], top[3], top[4], top[5], top_name, scores[(current_POV == 0) ? 1 : 0],
             bottom[0], bottom[1], bottom[2], bottom[3], bottom[4], bottom[5], bottom_name, scores[(current_POV == 0) ? 0 : 1],
             msg);
}
