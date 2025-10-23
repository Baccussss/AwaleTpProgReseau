//------------- ICI C'EST LES TRAVAUX DE FUTURS FONCTIONS -------------//

#include <string.h>


#define HOUSES_PER_SIDE 6
typedef struct {
    int board[HOUSES_PER_SIDE * 2];
    int score[2];
    int current_player; // 0 or 1
} Awale;


//Interface joli à envoyer aux clients pour afficher la partie
string afficher_interface_jeu(int board[12], int scores[2], int current_player) {
    /*
     ___ ___ ___ ___ ___ ___
    | X | X | X | X | X | X |  [Joueur 2]
    |___|___|___|___|___|___|  [Score: X]
    | X | X | X | X | X | X |  [Joueur 1]
    |___|___|___|___|___|___|  [Score: X]

    Joueur X à toi de jouer !
    */
    char interface[512];
    snprintf(interface, sizeof(interface),
        " ___ ___ ___ ___ ___ ___ \n"
        "| %2d | %2d | %2d | %2d | %2d | %2d |  [Joueur 2]\n"
        "|___|___|___|___|___|___|  [Score: %d]\n"
        "| %2d | %2d | %2d | %2d | %2d | %2d |  [Joueur 1]\n"
        "|___|___|___|___|___|___|  [Score: %d]\n\n"
        "Joueur %d à toi de jouer !\n",
        board[11], board[10], board[9], board[8], board[7], board[6],
        scores[1],
        board[0], board[1], board[2], board[3], board[4], board[5],
        scores[0],
        current_player + 1
    );
    return string(interface);
}