// server_awale.c
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#include "awale.h" // <-- tes fonctions awale_*

#define BACKLOG 5

static void send_str(int fd, const char *s)
{
    size_t n = strlen(s);
    ssize_t w = send(fd, s, n, 0);
    (void)w;
}

static int recv_line(int fd, char *buf, size_t maxlen)
{
    size_t i = 0;
    while (i + 1 < maxlen)
    {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0)
            return 0; // déconnexion propre
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -1; // erreur
        }
        if (c == '\r')
            continue; // ignore CR
        buf[i++] = c;
        if (c == '\n')
            break;
    }
    buf[i] = '\0';
    return (int)i;
}

static void board_to_string(const Awale *g, char *out, size_t outsz)
{
    // Affichage textuel simple + scores sous chaque camp (comme demandé)
    // On écrit dans 'out' (pas de printf direct).
    char line[256];
    size_t used = 0;
#define APPEND(fmt, ...)                                          \
    do                                                            \
    {                                                             \
        int n = snprintf(line, sizeof(line), fmt, ##__VA_ARGS__); \
        if (n < 0)                                                \
            n = 0;                                                \
        if (used + (size_t)n < outsz)                             \
        {                                                         \
            memcpy(out + used, line, n);                          \
            used += n;                                            \
        }                                                         \
    } while (0)

    APPEND("\n========== PLATEAU D'AWALE ==========\n\n");
    APPEND("  Camp du Joueur 2 (haut)\n  ");
    for (int i = HOUSES_PER_SIDE - 1; i >= 0; --i)
    {
        APPEND(" [%2d]", g->board[HOUSES_PER_SIDE + i]);
    }
    APPEND("\n  Score J2 : %d\n\n", g->score[1]);

    APPEND("  Camp du Joueur 1 (bas)\n  ");
    for (int i = 0; i < HOUSES_PER_SIDE; ++i)
    {
        APPEND(" [%2d]", g->board[i]);
    }
    APPEND("\n  Score J1 : %d\n\n", g->score[0]);

    APPEND("  --> Au Joueur %d de jouer <--\n\n", g->current_player + 1);
    out[used] = '\0';
#undef APPEND
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind");
        return 1;
    }
    if (listen(listenfd, BACKLOG) < 0)
    {
        perror("listen");
        return 1;
    }

    printf("Serveur Awale: en attente de 2 joueurs sur le port %s...\n", argv[1]);

    int players[2] = {-1, -1};
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    for (int i = 0; i < 2; ++i)
    {
        players[i] = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
        if (players[i] < 0)
        {
            perror("accept");
            return 1;
        }
        printf("Joueur %d connecté depuis %s\n", i + 1, inet_ntoa(cli_addr.sin_addr));
        char hello[128];
        snprintf(hello, sizeof(hello),
                 "Bienvenue ! Vous êtes Joueur %d.\nEn attente de l'autre joueur...\n\n", i + 1);
        send_str(players[i], hello);
    }

    // Les deux joueurs sont connectés
    send_str(players[0], "Les deux joueurs sont présents. Vous êtes Joueur 1.\n");
    send_str(players[1], "Les deux joueurs sont présents. Vous êtes Joueur 2.\n");

    Awale g;
    awale_init(&g);

    // Envoi du plateau initial
    char buf[1024];
    board_to_string(&g, buf, sizeof(buf));
    send_str(players[0], buf);
    send_str(players[1], buf);

    while (1)
    {
        int p = g.current_player; // 0 -> J1 ; 1 -> J2
        int fd_curr = players[p];
        int fd_wait = players[1 - p];

        // Prompts
        send_str(fd_curr, "Votre tour. Choisissez une maison (0-5) puis Entrée:\n");
        send_str(fd_wait, "En attente du coup adverse...\n");

        // Lire une ligne du joueur courant
        char line[128];
        int r = recv_line(fd_curr, line, sizeof(line));
        if (r <= 0)
        {
            send_str(fd_wait, "L'autre joueur s'est déconnecté. Fin de partie.\n");
            break;
        }

        // Parser
        char *endptr = NULL;
        int h = (int)strtol(line, &endptr, 10);
        if (endptr == line)
        {
            send_str(fd_curr, "Entrée invalide. Tapez un entier entre 0 et 5.\n");
            continue;
        }

        // Tenter le coup
        if (!awale_move(&g, h))
        {
            send_str(fd_curr, "Coup invalide. Réessayez (0-5, maison non vide, règles OK).\n");
            continue;
        }

        // Diffuser l'état
        board_to_string(&g, buf, sizeof(buf));
        send_str(players[0], buf);
        send_str(players[1], buf);

        // Détection fin simple (tu peux remplacer par ta version complète)
        if (awale_is_game_over(&g))
        {
            char endmsg[256];
            snprintf(endmsg, sizeof(endmsg),
                     "Partie terminée. Scores : J1=%d, J2=%d\n%s\n",
                     g.score[0], g.score[1],
                     (g.score[0] > g.score[1]) ? "J1 gagne !" : (g.score[1] > g.score[0]) ? "J2 gagne !"
                                                                                          : "Égalité !");
            send_str(players[0], endmsg);
            send_str(players[1], endmsg);
            break;
        }
    }

    close(players[0]);
    close(players[1]);
    close(listenfd);
    return 0;
}