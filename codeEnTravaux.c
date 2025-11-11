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
#include <signal.h>
#include <pthread.h>

#include "awale.h" // Awale + afficher_interface_jeu

#define BACKLOG 5 //nb max de connexions en attente
#define MAX_JOUEURS 2
#define MAX_AMIS 16
#define MAX_PSEUDO_LEN 32
#define MAX_BIO_LEN 128

typedef struct
{
    int fd;
    char pseudo[MAX_PSEUDO_LEN];
    char bio[MAX_BIO_LEN];
    char amis[MAX_AMIS][MAX_PSEUDO_LEN];
    int nb_amis;
    bool est_joueur;
    bool en_ligne;
    int id_partie; // -1 si pas en partie
} joueur_t;

// ------------- Initialisation des variables globales
joueur_t joueurs[MAX_JOUEURS];
pthread_mutex_t mutex_joueurs = PTHREAD_MUTEX_INITIALIZER;


// ------------- Prototypes des fonctions
void *gerer_client(void *arg);
joueur_t *gerer_connexion(char *pseudo, int socket_client);
void gerer_deconnexion(joueur_t *joueur);
int envoyer_message(int socket, const char *message);
void menu(joueur_t *joueur);


//----- Partie initialisation du serveur et gestion des sockets
//Piqué sur les exemples du TP1

// On s'assure que tout le buffer part
static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t w = send(fd, buf + sent, len - sent, 0);
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (w == 0)
            return -1;
        sent += (size_t)w;
    }
    return 0;
}
// fonction d'envoi d'une chaîne de caractères à un socket
int envoyer_message(int socket, const char *message) 
{ 
    return send_all(socket, message, strlen(message)); 
}

// Lecture d'une ligne terminée par '\n'
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
            return -1;
        }
        if (c == '\r')
            continue; // ignorer CR
        buf[i++] = c;
        if (c == '\n')
            break;
    }
    buf[i] = '\0';
    return (int)i;
}

joueur_t *gerer_connexion(char *pseudo, int socket_client)
{
    pthread_mutex_lock(&mutex_joueurs);
    for (int i = 0; i < MAX_JOUEURS; ++i)
    {
        if (!joueurs[i].en_ligne)
        {
            joueurs[i].en_ligne = true;
            joueurs[i].fd = socket_client;
            strncpy(joueurs[i].pseudo, pseudo, sizeof(joueurs[i].pseudo) - 1);
            joueurs[i].pseudo[sizeof(joueurs[i].pseudo) - 1] = '\0';
            pthread_mutex_unlock(&mutex_joueurs);
            return &joueurs[i];
        }
    }
    pthread_mutex_unlock(&mutex_joueurs);
    return NULL; // pas de place
}
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN); // éviter un crash si un client coupe pendant send()

    // ----------- Socket d'écoute ---
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        perror("erreur lors de la creation de la socket");
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
        perror("erreur lors du bind");
        close(listenfd);
        return 1;
    }
    if (listen(listenfd, BACKLOG) < 0)
    {
        perror("erreur lors du listen");
        return 1;
    }

    printf("Serveur Awale Actif sur le port %s...\n", argv[1]);

    // --- Accepter exactement deux joueurs ---
    joueur_t joueurs[2];
    for (int i = 0; i < 2; ++i)
    {
        joueurs[i].fd = -1;
        joueurs[i].pseudo[0] = '\0';
        joueurs[i].bio[0] = '\0';
        joueurs[i].nb_amis = 0;
        joueurs[i].est_joueur = true;
        joueurs[i].en_ligne = false;
    }
    struct sockaddr_in cli_addr;
    socklen_t clilen;

    for (int i = 0; i < 2; ++i)
    {
        clilen = sizeof(cli_addr);
        int fd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
        if (fd < 0)
        {
            perror("erreur lors de l'acceptation de la co");
            return 1;
        }
        joueurs[i].fd = fd;
        joueurs[i].en_ligne = true;
        snprintf(joueurs[i].pseudo, sizeof(joueurs[i].pseudo), "J%d", i + 1); // default username
        printf("Joueur %d connecté depuis %s\n", i + 1, inet_ntoa(cli_addr.sin_addr));
        char hello[160];
        snprintf(hello, sizeof(hello), "Bienvenue ! Vous êtes Joueur %d.\nEn attente de l'autre joueur...\n\n", i + 1);
        send_str(joueurs[i].fd, hello);

        // Demander le pseudo au client et l'enregistrer
        send_str(joueurs[i].fd, "Entrez votre pseudo (max 31 caractères) puis Entrée :\n");
        char namebuf[128];
        int rr = recv_line(joueurs[i].fd, namebuf, sizeof(namebuf));
        if (rr <= 0)
        {
            // lecture échouée -> garder le pseudo par défaut
            char confirm[80];
            snprintf(confirm, sizeof(confirm), "Pas de pseudo reçu. Vous êtes %s\n\n", joueurs[i].pseudo);
            send_str(joueurs[i].fd, confirm);
        }
        else
        {
            // enlever \r et \n en fin
            size_t L = strlen(namebuf);
            while (L > 0 && (namebuf[L - 1] == '\n' || namebuf[L - 1] == '\r'))
                namebuf[--L] = '\0';
            if (L == 0)
            {
                char confirm[80];
                snprintf(confirm, sizeof(confirm), "Pseudo vide, vous êtes %s\n\n", joueurs[i].pseudo);
                send_str(joueurs[i].fd, confirm);
            }
            else
            {
                /* copier de façon sûre et éviter l'avertissement de troncature */
                snprintf(joueurs[i].pseudo, sizeof(joueurs[i].pseudo), "%s", namebuf);
                char confirm[128];
                snprintf(confirm, sizeof(confirm), "Bonjour %s ! En attente de l'autre joueur...\n\n", joueurs[i].pseudo);
                send_str(joueurs[i].fd, confirm);
            }
        }
    }








    // Les deux joueurs sont connectés
    char readybuf[160];
    snprintf(readybuf, sizeof(readybuf), "Les deux joueurs sont présents. Vous êtes %s.\n", joueurs[0].pseudo);
    send_str(joueurs[0].fd, readybuf);
    snprintf(readybuf, sizeof(readybuf), "Les deux joueurs sont présents. Vous êtes %s.\n", joueurs[1].pseudo);
    send_str(joueurs[1].fd, readybuf);

    // État de jeu
    Awale g;
    awale_init(&g);

    // UI initiale POV pour chaque joueur
    char ui0[1024], ui1[1024];
    afficher_interface_jeu(ui0, sizeof(ui0), g.board, g.score, g.current_player, 0, joueurs[0].pseudo, joueurs[1].pseudo); // POV J1
    afficher_interface_jeu(ui1, sizeof(ui1), g.board, g.score, g.current_player, 1, joueurs[1].pseudo, joueurs[0].pseudo); // POV J2
    send_str(joueurs[0].fd, ui0);
    send_str(joueurs[1].fd, ui1);

    // Boucle de jeu
    while (1)
    {
        int p = g.current_player; // 0 -> J1 ; 1 -> J2
        int fd_curr = joueurs[p].fd;
        int fd_wait = joueurs[1 - p].fd;

        // Prompt uniquement au joueur courant
        send_str(fd_curr, "Entrez un nombre (0-5) puis Entrée:\n");

        // Lire la saisie
        char line[128];
        int r = recv_line(fd_curr, line, sizeof(line));
        if (r <= 0)
        { // déconnexion ou erreur
            send_str(fd_wait, "L'autre joueur s'est déconnecté. Fin de partie.\n");
            break;
        }

        // Parser l'entier
        char *endptr = NULL;
        int h = (int)strtol(line, &endptr, 10);
        if (endptr == line || h < 0 || h >= HOUSES_PER_SIDE)
        {
            send_str(fd_curr, "Entrée invalide. Tapez un entier entre 0 et 5.\n");
            // Réafficher l'UI pour rester synchro
            afficher_interface_jeu(ui0, sizeof(ui0), g.board, g.score, g.current_player, 0, joueurs[0].pseudo, joueurs[1].pseudo);
            afficher_interface_jeu(ui1, sizeof(ui1), g.board, g.score, g.current_player, 1, joueurs[1].pseudo, joueurs[0].pseudo);
            send_str(joueurs[0].fd, ui0);
            send_str(joueurs[1].fd, ui1);
            continue;
        }

        // Tenter le coup (index relatif au joueur courant)
        if (!awale_move(&g, h))
        {
            send_str(fd_curr, "Coup invalide (maison vide / règle). Réessayez.\n");
            afficher_interface_jeu(ui0, sizeof(ui0), g.board, g.score, g.current_player, 0, joueurs[0].pseudo, joueurs[1].pseudo);
            afficher_interface_jeu(ui1, sizeof(ui1), g.board, g.score, g.current_player, 1, joueurs[1].pseudo, joueurs[0].pseudo);
            send_str(joueurs[0].fd, ui0);
            send_str(joueurs[1].fd, ui1);
            continue;
        }

        // Coup accepté → diffuser l'état POV aux deux
        afficher_interface_jeu(ui0, sizeof(ui0), g.board, g.score, g.current_player, 0, joueurs[0].pseudo, joueurs[1].pseudo);
        afficher_interface_jeu(ui1, sizeof(ui1), g.board, g.score, g.current_player, 1, joueurs[1].pseudo, joueurs[0].pseudo);
        send_str(joueurs[0].fd, ui0);
        send_str(joueurs[1].fd, ui1);
        // Fin de partie ? (version simple)
        if (awale_is_game_over(&g))
        {
            char endmsg[512];
            char resultbuf[128];
            const char *resultmsg;
            if (g.score[0] > g.score[1])
            {
                snprintf(resultbuf, sizeof(resultbuf), "%s gagne !", joueurs[0].pseudo);
                resultmsg = resultbuf;
            }
            else if (g.score[1] > g.score[0])
            {
                snprintf(resultbuf, sizeof(resultbuf), "%s gagne !", joueurs[1].pseudo);
                resultmsg = resultbuf;
            }
            else
            {
                resultmsg = "Égalité !";
            }
            snprintf(endmsg, sizeof(endmsg), "Partie terminée. Scores : %s=%d, %s=%d\n%s\n", joueurs[0].pseudo, g.score[0], joueurs[1].pseudo, g.score[1], resultmsg);
            send_str(joueurs[0].fd, endmsg);
            send_str(joueurs[1].fd, endmsg);
            break;
        }
    }

    // Nettoyage
    if (joueurs[0].fd >= 0)
        close(joueurs[0].fd);
    if (joueurs[1].fd >= 0)
        close(joueurs[1].fd);
    close(listenfd);
    return 0;
}