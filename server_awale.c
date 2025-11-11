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

#define BACKLOG 10 // nb max de connexions en attente
#define MAX_JOUEURS 100
#define MAX_PSEUDO_LEN 32
#define MAX_BIO_LEN 128
#define TAILLE_BUFFER 1024

typedef struct
{
    int fd;
    char pseudo[MAX_PSEUDO_LEN];
    char bio[MAX_BIO_LEN];
    bool en_ligne;
    int id_partie; // -1 si pas en partie
} joueur_t;


// ------------- Initialisation des variables globales
joueur_t joueurs[MAX_JOUEURS];
pthread_mutex_t mutex_joueurs = PTHREAD_MUTEX_INITIALIZER;

// ------------ Prototypes des fonctions
void *gerer_client(void *arg);
joueur_t *gerer_connexion(char *pseudo, int socket_client);
void gerer_deconnexion(joueur_t *joueur);
int envoyer_message(int sockfd, const char *message);
joueur_t *trouver_joueur_par_pseudo(const char *pseudo);
void menu(joueur_t *joueur);



//-------------- Partie initialisation du serveur et gestion des sockets
// Piqué sur les exemples du TP1
// Envoi robuste: s'assure que tout le buffer part
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
int envoyer_message(int sockfd, const char *message)
{
    return send_all(sockfd, message, strlen(message));
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

// Trouver un joueur par son pseudo
joueur_t *trouver_joueur_par_pseudo(const char *pseudo)
{
    for (int i = 0; i < MAX_JOUEURS; i++)
    {
        if (strcmp(joueurs[i].pseudo, pseudo) == 0)
        {
            return &joueurs[i];
        }
    }
    return NULL;
}

// Fonction pour gerer la connexion d'un joueur avec un PSEUDO
joueur_t *gerer_connexion(char *pseudo, int socket_client)
{
    pthread_mutex_lock(&mutex_joueurs);

    // Vérifier si pseudo déjà en ligne
    joueur_t *existant = trouver_joueur_par_pseudo(pseudo);
    if (existant != NULL && existant->en_ligne)
    {
        envoyer_message(socket_client, "Ce pseudo est déjà en ligne!\n");
        pthread_mutex_unlock(&mutex_joueurs);
        return NULL;
    }

    // Trouver un slot libre ou réutiliser le joueur existant
    joueur_t *joueur = existant;
    if (joueur == NULL)
    {
        for (int i = 0; i < MAX_JOUEURS; i++)
        {
            if (joueurs[i].pseudo[0] == '\0')
            {
                joueur = &joueurs[i];
                snprintf(joueur->pseudo, sizeof(joueur->pseudo), "%s", pseudo);
                break;
            }
        }
    }

    if (joueur == NULL)
    {
        envoyer_message(socket_client, "Serveur plein!\n");
        pthread_mutex_unlock(&mutex_joueurs);
        return NULL;
    }

    joueur->fd = socket_client;
    joueur->en_ligne = true;
    joueur->id_partie = -1;

    envoyer_message(socket_client, "Connexion réussie!\n");
    printf("Joueur connecté: %s\n", pseudo);

    pthread_mutex_unlock(&mutex_joueurs);
    return joueur;
}

// Gere la deconnexion d'un joueur et donc la libération de son slot
void gerer_deconnexion(joueur_t *joueur)
{
    envoyer_message(joueur->fd, "Déconnexion...\n");
    printf("Déconnexion: %s\n", joueur->pseudo);

    pthread_mutex_lock(&mutex_joueurs);
    joueur->en_ligne = false;
    joueur->id_partie = -1;
    close(joueur->fd);
    joueur->fd = -1;
    pthread_mutex_unlock(&mutex_joueurs);

    pthread_exit(NULL);
}


void *gerer_client(void *arg)
{
    int socket_client = *((int *)arg);
    free(arg);

    char buffer[TAILLE_BUFFER];
    joueur_t *joueur = NULL;

    // Boucle de connexion
    while (joueur == NULL)
    {
        envoyer_message(socket_client, "Entrez votre pseudo (max 31 caractères):\n");

        int n = recv_line(socket_client, buffer, sizeof(buffer));
        if (n <= 0)
        {
            close(socket_client);
            return NULL;
        }

        // Enlever les retours à la ligne
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
        {
            buffer[--len] = '\0';
        }

        if (len == 0)
        {
            envoyer_message(socket_client, "Pseudo vide! Réessayez.\n");
            continue;
        }

        joueur = gerer_connexion(buffer, socket_client);
    }

    // Entrer dans la boucle de menu
    menu(joueur);
    return NULL;
}


// ----------------- Le grand menu des commandes
void menu(joueur_t *joueur)
{
    char buffer[TAILLE_BUFFER];
    char commande[32];

    while (1)
    {
        int octets = recv_line(joueur->fd, buffer, sizeof(buffer));
        if (octets <= 0)
        {
            gerer_deconnexion(joueur);
            return;
        }

        // Enlever les retours à la ligne
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
        {
            buffer[--len] = '\0';
        }

        sscanf(buffer, "%s", commande);

        if (strcmp(commande, "LOGOUT") == 0)
        {
            gerer_deconnexion(joueur);
        }
        else if (strcmp(commande, "HELP") == 0)
        {
            envoyer_message(joueur->fd, "Commandes disponibles:\n"
                                        "LOGOUT - Se déconnecter\n"
                                        "HELP - Afficher cette aide\n");
        }
        else
        {
            envoyer_message(joueur->fd, "Commande inconnue. Tapez HELP pour l'aide.\n");
        }

        memset(buffer, 0, sizeof(buffer));
    }
}

// ----------------- Main pour initialiser le serveur et accepter les connexions
// a la suite du main les sockets clients sont gdans des threads séparés
// geres par la fonction gerer_client
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN); // éviter un crash si un client coupe pendant send()

    // Initialiser le tableau de joueurs
    for (int i = 0; i < MAX_JOUEURS; i++)
    {
        joueurs[i].fd = -1;
        joueurs[i].pseudo[0] = '\0';
        joueurs[i].bio[0] = '\0';
        joueurs[i].en_ligne = false;
        joueurs[i].id_partie = -1;
    }

    // Créer la socket d'écoute
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("erreur lors de la creation de la socket");
        return 1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("erreur lors du bind");
        close(sockfd);
        return 1;
    }

    if (listen(sockfd, BACKLOG) < 0)
    {
        perror("erreur lors du listen");
        close(sockfd);
        return 1;
    }

    printf("Serveur Awale actif sur le port %s...\n", argv[1]);

    // Boucle d'acceptation infinie
    while (1)
    {
        struct sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);

        int newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0)
        {
            perror("erreur lors de l'acceptation de la connexion");
            continue;
        }

        printf("Connexion acceptée depuis %s:%d\n",
               inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        int *socket_client = malloc(sizeof(int));
        *socket_client = newsockfd;

        //c'est ici qu'on crée le thread pour gérer le client et qu'on appelle gerer_client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, gerer_client, socket_client) != 0) 
        {
            perror("erreur lors de la création du thread");
            close(newsockfd);
            free(socket_client);
        }
        else
        {
            pthread_detach(thread_id);
        }
    }

    close(sockfd);
    return 0;
}