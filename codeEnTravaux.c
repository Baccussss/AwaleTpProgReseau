//------------- ICI C'EST LES TRAVAUX DE FUTURS FONCTIONS -------------//
// Ecrit l'interface dans 'out' (taille out_sz). 
// POV: 0=Joueur1, 1=Joueur2.
// Retourne le nombre de caractères écrits (comme snprintf).
#include <stdio.h>
#include <string.h>

#define HOUSES_PER_SIDE 6
typedef struct {
    int board[HOUSES_PER_SIDE * 2];
    int score[2];
    int current_player; // 0 or 1
} Awale;

// Renvoie une chaîne de caractères formatée représentant l'interface du jeu
// POV: 0=Joueur1, 1=Joueur2.
char* afficher_interface_jeu(char *out, size_t out_sz,
                              const int board[12], const int scores[2],
                              int current_player, int current_POV) {
    /*
    / Réorganise le plateau selon le point de vue (POV)
    / change le message selon si c'est le tour du joueur ou pas
    / Pour utiliser la fonction :
    / char ui[1024];
    / afficher_interface_jeu(ui, sizeof(ui), board, scores, current_player, current_POV);
    / printf("%s", ui);
    */
    int top[6], bottom[6];
    int top_player, bottom_player;

    if (current_POV == 0) {
        // Vue Joueur 1: bas = 0..5, haut = 11..6
        for (int i = 0; i < 6; ++i) {
            bottom[i] = board[i];          // 0..5
            top[i]    = board[11 - i];     // 11..6
        }
        bottom_player = 1; // J1
        top_player    = 2; // J2
    } else {
        // Vue Joueur 2: bas = 6..11, haut = 5..0
        for (int i = 0; i < 6; ++i) {
            bottom[i] = board[6 + i];      // 6..11
            top[i]    = board[5 - i];      // 5..0
        }
        bottom_player = 2; // J2
        top_player    = 1; // J1
    }

    char msg[160];
    if (current_player == current_POV) {
        // C'est votre tour
        snprintf(msg, sizeof(msg),
                 "A vous de jouer (J%d) — Choisissez une maison (0-5) puis Entrée\n",
                 current_POV + 1);
    } else {
        // En attente de l'autre joueur
        snprintf(msg, sizeof(msg),
                 "En attente du coup adverse de (J%d)...\n",
                 current_player + 1);
    }

    snprintf(out, out_sz,
        "\n========== PLATEAU D'AWALE ==========\n\n"
        " ___  ___  ___  ___  ___  ___ \n"
        "|%2d |%2d |%2d |%2d |%2d |%2d |  [Joueur %d]\n"
        "|___|___|___|___|___|___|  [Score: %d]\n"
        "|%2d |%2d |%2d |%2d |%2d |%2d |  [Joueur %d]\n"
        "|___|___|___|___|___|___|  [Score: %d]\n\n"
        "%s",
        top[0], top[1], top[2], top[3], top[4], top[5], top_player, scores[top_player - 1],
        bottom[0], bottom[1], bottom[2], bottom[3], bottom[4], bottom[5], bottom_player, scores[bottom_player - 1],
        msg
    );

    return out; // Renvoie le tampon formatté
}




int main() {
    // Exemple de plateau et scores
    int board[12] = {0, 5, 3, 4, 2, 4, 0, 1, 4, 3, 4, 7};
    int scores[2] = {0, 3};
    int current_player = 0; // Joueur 1
    int current_POV = 0; // Point de vue Joueur 1

    // Déclare un buffer pour stocker l'interface
    char ui[1024];

    printf("Affichage du point de vue du Joueur 1 à son tour\n");
    // Appelle la fonction et récupère le résultat dans ui
    afficher_interface_jeu(ui, sizeof(ui), board, scores, 0, 0);
    printf("%s", ui);
    printf("Affichage du point de vue du Joueur 2 au tour du Joueur 1\n");
    afficher_interface_jeu(ui, sizeof(ui), board, scores, 0, 1);
    printf("%s", ui);
    printf("Affichage du point de vue du Joueur 1 au tour du Joueur 2\n");
    afficher_interface_jeu(ui, sizeof(ui), board, scores, 1, 0);
    printf("%s", ui);
    printf("Affichage du point de vue du Joueur 2 à son tour\n");
    afficher_interface_jeu(ui, sizeof(ui), board, scores, 1, 1);
    printf("%s", ui);



    return 0;
}
