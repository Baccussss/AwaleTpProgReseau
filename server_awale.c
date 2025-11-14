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
// utilisation de threads pour que le server soit pas ralentit par des clients lents
// chaque client est géré dans un thread séparé de manière independante

#include "awale.h" // Awale + afficher_interface_jeu

#define BACKLOG 10 // nb max de connexions en attente
#define MAX_JOUEURS 100
#define MAX_PSEUDO_LEN 32
#define MAX_BIO_LEN 128
#define MAX_AMIS 20
#define TAILLE_BUFFER 1024
#define MAX_NB_PARTIES 50
#define MAX_OBSERVATEURS 10

// Structure représentant un joueur
typedef struct
{
    int fd;
    char pseudo[MAX_PSEUDO_LEN];
    char bio[MAX_BIO_LEN];
    char amis[MAX_AMIS][MAX_PSEUDO_LEN];
    int nb_amis;
    bool en_ligne;
    int id_partie;                            // -1 si pas en partie
    char demande_defi_depuis[MAX_PSEUDO_LEN]; // pseudo de qui a défié ce joueur (vide si aucun)
    bool demande_defi_est_privee;            // si le défi est pour une partie privée
} joueur_t;
// Structure représentant une partie
typedef struct
{
    int id;
    Awale jeu;
    joueur_t *joueur1;
    joueur_t *joueur2;
    char observateurs[MAX_OBSERVATEURS][MAX_PSEUDO_LEN];
    bool est_privee;
    bool en_cours;
} partie_t;

// ------------- Initialisation des variables globales
joueur_t joueurs[MAX_JOUEURS];
pthread_mutex_t mutex_joueurs = PTHREAD_MUTEX_INITIALIZER;
// le mutex permet de limiter l'accès concurrent aux données des joueurs (quand on touche le tableau joueurs sur plusieurs lignes)
//  pour éviter les conflits

partie_t parties[MAX_NB_PARTIES];
int nb_parties_actives = 0;
pthread_mutex_t mutex_parties = PTHREAD_MUTEX_INITIALIZER; // mutex aussi pour les parties

// ------------ Prototypes des fonctions
// Gestion des sockets et des clients
void *gerer_client(void *arg);
joueur_t *gerer_connexion(char *pseudo, int socket_client);
void gerer_deconnexion(joueur_t *joueur);
int envoyer_message(int sockfd, const char *message);
joueur_t *trouver_joueur_par_pseudo(const char *pseudo); // très utile

// Gestion defi d'un joueur par un autre
void afficher_joueurs_en_ligne(joueur_t *joueur);
void gerer_defi(joueur_t *joueur, char *buffer); // gère la demande de défi
void gerer_accepter(joueur_t *joueur);
void gerer_refuser(joueur_t *joueur);
partie_t *creer_partie(joueur_t *j1, joueur_t *j2, bool est_privee);
//partie privee

// Gestion des commandes lors de la partie
void jouer_coup(joueur_t *joueur, int maison);
void envoyer_plateau_aux_joueurs(partie_t *partie); // pq prend pas message : on avait déjà codé l'affichage directement dans Awale.c avant de savoir qu'on passerait par un menu
void terminer_partie(partie_t *partie);

// Gestion de l'observation d'un plateau
partie_t *trouver_partie_joueur(joueur_t *joueur);
void afficher_parties_en_cours(joueur_t *joueur);
void observer_partie(joueur_t *joueur, char *buffer);
void quitter_observation(joueur_t *joueur);
void menu(joueur_t *joueur);

// Gestion du chat
void gerer_chat_prive(joueur_t *emetteur, char *message);

// Gestion de la bio
void gerer_bio(joueur_t *joueur, char *buffer);

// Déclaration pour consulter les infos d'un joueur
void gerer_info(joueur_t *joueur, char *buffer);

// Gestion des amis
void gerer_ajouter_ami(joueur_t *joueur, char *buffer);
void gerer_consulter_ami(joueur_t *joueur);

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
    /*
    Récupère une ligne depuis le socket fd, la met dans buf,
    si detection du \n ou ligne trop longue alors met le \0 de fin de ligne
    et retourne le nombre de caractères lus (sans le \0).
    */
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
    joueur->demande_defi_depuis[0] = '\0';
    // si c'est un nouveau slot (pseudo initialisé), s'assurer que la liste d'amis est vide
    if (existant == NULL)
    {
        joueur->nb_amis = 0;
        for (int k = 0; k < MAX_AMIS; k++)
            joueur->amis[k][0] = '\0';
    }

    envoyer_message(socket_client, "Connexion réussie!\n");
    // on envoie les commandes de menu tout de suite et non pas avec la fonction menu
    // car sinon menu() l'afficherait à chaque commande ce qui pollurait l'affichage
    envoyer_message(joueur->fd, "Commandes disponibles:\n"
                                "DECO - Se déconnecter\n"
                                "HELP - Afficher cette aide\n"
                                "LISTEJ - Lister les joueurs en ligne\n"
                                "LISTEP - Lister les parties en cours\n"
                                "DEFIPU <pseudo> - Défier un joueur sur une partie publique\n"
                                "DEFIPR <pseudo> - Défier un joueur sur une partie privée\n"
                                "ACCEPTER - Accepter un défi en attente\n"
                                "REFUSER - Refuser un défi en attente\n"
                                "JOUER <0-5> - Jouer un coup (lors d'une partie)\n"
                                "BIO <texte> - Écrire votre bio\n"
                                "INFO <pseudo> - Voir pseudo et bio d'un joueur\n"
                                "AJOUTERAMI <pseudo> - Ajouter <pseudo> à votre liste d'amis\n"
                                "CONSULTERAMI - Afficher votre liste d'amis\n"
                                "OBSERVER <id_partie> - Observer une partie en cours\n"
                                "QUITTEROBS - Quitter l'observation d'une partie\n"
                                "MSG <pseudo> <message> - Envoyer un message privé à <pseudo>\n");
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
    // nettoyer les défis en attente provenant de ce joueur chez les autres
    for (int i = 0; i < MAX_JOUEURS; i++)
    {
        if (joueurs[i].demande_defi_depuis[0] != '\0' && strcmp(joueurs[i].demande_defi_depuis, joueur->pseudo) == 0)
        {
            joueurs[i].demande_defi_depuis[0] = '\0';
            if (joueurs[i].en_ligne)
            {
                envoyer_message(joueurs[i].fd, "Le joueur qui vous a défié s'est déconnecté. Défi annulé.\n");
            }
        }
    }
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

//-------------- Partie defi d'un joueur par un autre et commandes lors de la partie
// la fonction est plutot clair je dirais
void afficher_joueurs_en_ligne(joueur_t *joueur)
{
    pthread_mutex_lock(&mutex_joueurs);

    char message[TAILLE_BUFFER] = "Joueurs en ligne:\n";
    int count = 0;

    for (int i = 0; i < MAX_JOUEURS; i++)
    {
        if (joueurs[i].en_ligne)
        {
            strcat(message, "  - ");
            strcat(message, joueurs[i].pseudo);
            // affiche si le joueur est en partie pour savoir à qui on peut envoyer un défi
            if (joueurs[i].id_partie != -1)
            {
                strcat(message, " (en partie)");
            }
            // affiche si c'est le joueur qui a demandé la liste
            if (strcmp(joueurs[i].pseudo, joueur->pseudo) == 0)
            {
                strcat(message, " (c'est toi !)");
            }
            strcat(message, "\n");
            count++;
        }
    }

    if (count == 0)
    {
        strcat(message, "  Aucun autre joueur en ligne.\n");
    }

    envoyer_message(joueur->fd, message);
    pthread_mutex_unlock(&mutex_joueurs);
}

// Créer une nouvelle partie
partie_t *creer_partie(joueur_t *j1, joueur_t *j2, bool est_privee)
{
    pthread_mutex_lock(&mutex_parties);

    partie_t *partie = NULL;
    for (int i = 0; i < MAX_NB_PARTIES; i++)
    {
        if (!parties[i].en_cours)
        {
            partie = &parties[i];
            partie->id = i;
            partie->joueur1 = j1;
            partie->joueur2 = j2;
            awale_init(&partie->jeu);
            partie->est_privee = est_privee;
            partie->en_cours = true;

            j1->id_partie = i;
            j2->id_partie = i;

            nb_parties_actives++;
            break;
        }
    }

    pthread_mutex_unlock(&mutex_parties);
    return partie;
}


// Gerer defi publique
void gerer_defi_publique(joueur_t *joueur, char *buffer)
{
    char pseudo_adversaire[MAX_PSEUDO_LEN];

    if (sscanf(buffer, "DEFIPU %s", pseudo_adversaire) != 1)
    {
        envoyer_message(joueur->fd, "Format invalide. Utiliser: DEFI <pseudo>\n");
        return;
    }

    if (joueur->id_partie != -1)
    {
        envoyer_message(joueur->fd, "Vous êtes déjà dans une partie!\n");
        return;
    }

    joueur_t *adversaire = trouver_joueur_par_pseudo(pseudo_adversaire);
    if (!adversaire || !adversaire->en_ligne)
    {
        envoyer_message(joueur->fd, "Joueur non trouvé ou hors ligne.\n");
        return;
    }

    if (adversaire->id_partie != -1)
    {
        envoyer_message(joueur->fd, "Ce joueur est déjà en partie.\n");
        return;
    }

    // Demander l'acceptation au joueur adverse (simple mécanisme de pending)
    pthread_mutex_lock(&mutex_joueurs);
    if (adversaire->demande_defi_depuis[0] != '\0')
    {
        // Quelqu'un d'autre a déjà défié cet adversaire
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Ce joueur a déjà un défi en attente.\n");
        return;
    }

    // enregistrer le défi en attente
    snprintf(adversaire->demande_defi_depuis, sizeof(adversaire->demande_defi_depuis), "%s", joueur->pseudo);
    adversaire->demande_defi_est_privee = false;
    pthread_mutex_unlock(&mutex_joueurs);

    char msg[256];
    snprintf(msg, sizeof(msg), "Demande de défi envoyée à %s.\n", adversaire->pseudo);
    envoyer_message(joueur->fd, msg);

    snprintf(msg, sizeof(msg), "Vous avez reçu un défi publique de %s. Tapez ACCEPTER ou REFUSER.\n", joueur->pseudo);
    envoyer_message(adversaire->fd, msg);
}

// Gerer defi prive (pour facilite le code j'ai copie colle notre fonction de defi publique et modifié juste ce qu'il faut, c'etait un peu le rush)
void gerer_defi_prive(joueur_t *joueur, char *buffer)
{
    char pseudo_adversaire[MAX_PSEUDO_LEN];

    if (sscanf(buffer, "DEFIPR %s", pseudo_adversaire) != 1)
    {
        envoyer_message(joueur->fd, "Format invalide. Utiliser: DEFI <pseudo>\n");
        return;
    }

    if (joueur->id_partie != -1)
    {
        envoyer_message(joueur->fd, "Vous êtes déjà dans une partie!\n");
        return;
    }

    joueur_t *adversaire = trouver_joueur_par_pseudo(pseudo_adversaire);
    if (!adversaire || !adversaire->en_ligne)
    {
        envoyer_message(joueur->fd, "Joueur non trouvé ou hors ligne.\n");
        return;
    }

    if (adversaire->id_partie != -1)
    {
        envoyer_message(joueur->fd, "Ce joueur est déjà en partie.\n");
        return;
    }

    // Demander l'acceptation au joueur adverse (simple mécanisme de pending)
    pthread_mutex_lock(&mutex_joueurs);
    if (adversaire->demande_defi_depuis[0] != '\0')
    {
        // Quelqu'un d'autre a déjà défié cet adversaire
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Ce joueur a déjà un défi en attente.\n");
        return;
    }

    // enregistrer le défi en attente
    snprintf(adversaire->demande_defi_depuis, sizeof(adversaire->demande_defi_depuis), "%s", joueur->pseudo);
    adversaire->demande_defi_est_privee = true;
    pthread_mutex_unlock(&mutex_joueurs);

    char msg[256];
    snprintf(msg, sizeof(msg), "Demande de défi privé envoyée à %s.\n", adversaire->pseudo);
    envoyer_message(joueur->fd, msg);

    snprintf(msg, sizeof(msg), "Vous avez reçu un défi privé de %s. Tapez ACCEPTER ou REFUSER.\n", joueur->pseudo);
    envoyer_message(adversaire->fd, msg);
}


// Accepter un défi en attente
void gerer_accepter(joueur_t *joueur)
{
    pthread_mutex_lock(&mutex_joueurs);
    if (joueur->demande_defi_depuis[0] == '\0')
    {
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Aucun défi en attente.\n");
        return;
    }

    // Trouver le challenger
    joueur_t *challenger = trouver_joueur_par_pseudo(joueur->demande_defi_depuis);
    if (!challenger || !challenger->en_ligne)
    {
        joueur->demande_defi_depuis[0] = '\0';
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Le joueur qui vous a défié n'est plus en ligne.\n");
        return;
    }

    // Vérifier que ni l'un ni l'autre ne sont déjà en partie
    if (challenger->id_partie != -1 || joueur->id_partie != -1)
    {
        joueur->demande_defi_depuis[0] = '\0';
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Impossible de démarrer la partie (un joueur est déjà en partie).\n");
        return;
    }

    // nettoyer l'état pending
    joueur->demande_defi_depuis[0] = '\0';
    pthread_mutex_unlock(&mutex_joueurs);

    // Créer la partie
    partie_t *partie = creer_partie(challenger, joueur, joueur->demande_defi_est_privee); // partie publique

    if (!partie)
    {
        envoyer_message(challenger->fd, "Impossible de créer la partie (serveur plein).\n");
        envoyer_message(joueur->fd, "Impossible de créer la partie (serveur plein).\n");
        return;
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "Partie créée avec %s!\n", joueur->pseudo);
    envoyer_message(challenger->fd, buf);
    snprintf(buf, sizeof(buf), "Partie créée avec %s!\n", challenger->pseudo);
    envoyer_message(joueur->fd, buf);

    // Envoyer le plateau initial
    envoyer_plateau_aux_joueurs(partie);
}

// Refuser un défi en attente
void gerer_refuser(joueur_t *joueur)
{
    pthread_mutex_lock(&mutex_joueurs);
    if (joueur->demande_defi_depuis[0] == '\0')
    {
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Aucun défi en attente.\n");
        return;
    }

    joueur_t *challenger = trouver_joueur_par_pseudo(joueur->demande_defi_depuis);
    if (challenger && challenger->en_ligne)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Votre défi a été refusé par %s.\n", joueur->pseudo);
        envoyer_message(challenger->fd, buf);
    }

    joueur->demande_defi_depuis[0] = '\0';
    pthread_mutex_unlock(&mutex_joueurs);
    envoyer_message(joueur->fd, "Vous avez refusé le défi.\n");
}

// Envoyer le plateau aux deux joueurs
void envoyer_plateau_aux_joueurs(partie_t *partie)
{
    char ui_j1[1024], ui_j2[1024], ui_obs[1024];
    // envoie aux joueurs
    afficher_interface_jeu(ui_j1, sizeof(ui_j1),
                           partie->jeu.board,
                           partie->jeu.score,
                           partie->jeu.current_player,
                           0, // POV joueur 1
                           partie->joueur1->pseudo,
                           partie->joueur2->pseudo);

    afficher_interface_jeu(ui_j2, sizeof(ui_j2),
                           partie->jeu.board,
                           partie->jeu.score,
                           partie->jeu.current_player,
                           1, // POV joueur 2
                           partie->joueur2->pseudo,
                           partie->joueur1->pseudo);

    afficher_interface_jeu(ui_obs, sizeof(ui_obs),
                           partie->jeu.board,
                           partie->jeu.score,
                           partie->jeu.current_player,
                           -1, // POV observateur
                           partie->joueur1->pseudo,
                           partie->joueur2->pseudo);

    envoyer_message(partie->joueur1->fd, ui_j1);
    envoyer_message(partie->joueur2->fd, ui_j2);
    for (int i = 0; i < MAX_OBSERVATEURS; i++)
    {
        if (partie->observateurs[i][0] != '\0')
        {
            joueur_t *observateur = trouver_joueur_par_pseudo(partie->observateurs[i]);
            if (observateur && observateur->en_ligne)
            {
                envoyer_message(observateur->fd, ui_obs);
            }
        }
    }
}

// Jouer un coup
void jouer_coup(joueur_t *joueur, int maison)
{
    partie_t *partie = trouver_partie_joueur(joueur);
    if (!partie)
    {
        envoyer_message(joueur->fd, "Vous n'êtes pas dans une partie!\n");
        return;
    }

    // Vérifier que c'est au tour du joueur
    int joueur_num = (partie->joueur1 == joueur) ? 0 : 1;
    if (partie->jeu.current_player != joueur_num)
    {
        envoyer_message(joueur->fd, "Ce n'est pas votre tour!\n");
        return;
    }

    // Tenter le coup
    if (!awale_move(&partie->jeu, maison))
    {
        envoyer_message(joueur->fd, "Coup invalide (maison vide ou règle non respectée). Réessayez.\n");
        envoyer_plateau_aux_joueurs(partie);
        return;
    }

    // Coup accepté, envoyer le nouveau plateau
    envoyer_plateau_aux_joueurs(partie);

    // Vérifier fin de partie
    if (awale_is_game_over(&partie->jeu))
    {
        char endmsg[512];
        const char *resultmsg;

        if (partie->jeu.score[0] > partie->jeu.score[1])
        {
            resultmsg = partie->joueur1->pseudo;
        }
        else if (partie->jeu.score[1] > partie->jeu.score[0])
        {
            resultmsg = partie->joueur2->pseudo;
        }
        else
        {
            resultmsg = "Égalité";
        }

        snprintf(endmsg, sizeof(endmsg),
                 "Partie terminée!\nScores: %s=%d, %s=%d\nRésultat: %s gagne!\n",
                 partie->joueur1->pseudo, partie->jeu.score[0],
                 partie->joueur2->pseudo, partie->jeu.score[1],
                 resultmsg);

        envoyer_message(partie->joueur1->fd, endmsg);
        envoyer_message(partie->joueur2->fd, endmsg);

        terminer_partie(partie);
    }
}

// Terminer une partie
void terminer_partie(partie_t *partie)
{
    pthread_mutex_lock(&mutex_parties);

    partie->joueur1->id_partie = -1;
    partie->joueur2->id_partie = -1;
    partie->en_cours = false;
    nb_parties_actives--;

    pthread_mutex_unlock(&mutex_parties);
}

// ----------------- La Gestion d'observation d'un plateau
// Trouver la partie d'un joueur
partie_t *trouver_partie_joueur(joueur_t *joueur)
{
    if (joueur->id_partie == -1)
    {
        return NULL;
    }
    return &parties[joueur->id_partie];
}

// Afficher les parties en cours
void afficher_parties_en_cours(joueur_t *joueur)
{
    pthread_mutex_lock(&mutex_parties);

    char message[TAILLE_BUFFER] = "Parties en cours:\n";
    int nb = 0;
    for (int i = 0; i < MAX_NB_PARTIES; i++)
    {
        if (parties[i].en_cours)
        {
            char buf[128]; // on embellit un peu l'affichage en précisant qui contre qui
            snprintf(buf, sizeof(buf), "  - Partie %d: %s vs %s\n",
                     parties[i].id,
                     parties[i].joueur1->pseudo,
                     parties[i].joueur2->pseudo);
            strcat(message, buf);
            nb++;
        }
    }

    if (nb == 0)
    {
        strcat(message, "  Aucune partie en cours.\n");
    }
    envoyer_message(joueur->fd, message);
    pthread_mutex_unlock(&mutex_parties);
}

// OBSERVER une partie
void observer_partie(joueur_t *joueur, char *buffer)
{
    int id_partie;
    if (sscanf(buffer, "OBSERVER %d", &id_partie) != 1)
    {
        envoyer_message(joueur->fd, "Format invalide. Utiliser: OBSERVER <id_partie>\n");
        return;
    }

    pthread_mutex_lock(&mutex_parties); // on fait des modif sur les parties donc on lock encore
    //check si la partie existe et est en cours
    if (id_partie < 0 || id_partie >= MAX_NB_PARTIES || !parties[id_partie].en_cours)
    {
        pthread_mutex_unlock(&mutex_parties);
        envoyer_message(joueur->fd, "Partie non trouvée.\n");
        return;
    }
    //check si la partie est privée et si oui alors est que le futur observateur est un ami d'un des joueurs
    if (parties[id_partie].est_privee)
    {
        bool est_ami = false;
        pthread_mutex_lock(&mutex_joueurs); //on va faire une recherche sur la liste d'amis donc on lock les joueurs
        for (int i = 0; i < MAX_AMIS; i++)
        {
            if (strcmp(parties[id_partie].joueur1->amis[i], joueur->pseudo) == 0 ||
                strcmp(parties[id_partie].joueur2->amis[i], joueur->pseudo) == 0)
            {
                est_ami = true;
                break;
            }
        }
        pthread_mutex_unlock(&mutex_joueurs);
        if (!est_ami)
        {
            pthread_mutex_unlock(&mutex_parties);
            envoyer_message(joueur->fd, "Cette partie est privée. Vous ne pouvez pas l'observer.\n"
                            "Demandez à un des joueurs de vous ajouter en ami pour observer.\n"
                            "Utiliser la commande MSG <pseudo> pour discuter\n");
            return;
        }
    }


    partie_t *partie = &parties[id_partie];
    // vérifier s'il y a de la place pour un observateur
    int obs_index = -1;
    for (int i = 0; i < MAX_OBSERVATEURS; i++)
    {
        if (partie->observateurs[i][0] == '\0')
        {
            obs_index = i;
            break;
        }
    }
    // si il y en a trop
    if (obs_index == -1)
    {
        pthread_mutex_unlock(&mutex_parties);
        envoyer_message(joueur->fd, "Nombre maximum d'observateurs atteint pour cette partie.\n");
        return;
    }

    // ajouter l'observateur à la partie
    snprintf(partie->observateurs[obs_index], MAX_PSEUDO_LEN, "%s", joueur->pseudo);
    pthread_mutex_unlock(&mutex_parties);
    // puis envoyer le plateau actuel de la partie (les prochains envois sont fait à chaque coup_joué dans jouer_coup)
    char msg[128];
    snprintf(msg, sizeof(msg), "Vous observez la partie %d de %s vs %s.\n", partie->id, partie->joueur1->pseudo, partie->joueur2->pseudo);
    envoyer_message(joueur->fd, msg);
    char ui_obs[1024];
    afficher_interface_jeu(ui_obs, sizeof(ui_obs),
                           partie->jeu.board,
                           partie->jeu.score,
                           partie->jeu.current_player,
                           -1, // -1 pour observateur voir Awale.c
                           partie->joueur1->pseudo,
                           partie->joueur2->pseudo);
    envoyer_message(joueur->fd, ui_obs);
}
// QUITTER OBSERVATION
void quitter_observation(joueur_t *joueur)
{
    pthread_mutex_lock(&mutex_parties);
    for (int i = 0; i < MAX_NB_PARTIES; i++)
    {
        partie_t *partie = &parties[i];
        if (partie->en_cours)
        {
            for (int j = 0; j < MAX_OBSERVATEURS; j++)
            {
                if (strcmp(partie->observateurs[j], joueur->pseudo) == 0)
                {
                    partie->observateurs[j][0] = '\0'; // retirer l'observateur
                    pthread_mutex_unlock(&mutex_parties);
                    envoyer_message(joueur->fd, "Vous avez quitté l'observation de la partie.\n\n");
                    envoyer_message(joueur->fd, "Commandes disponibles:\n"
                                "DECO - Se déconnecter\n"
                                "HELP - Afficher cette aide\n"
                                "LISTEJ - Lister les joueurs en ligne\n"
                                "LISTEP - Lister les parties en cours\n"
                                "DEFIPU <pseudo> - Défier un joueur sur une partie publique\n"
                                "DEFIPR <pseudo> - Défier un joueur sur une partie privée\n"
                                "ACCEPTER - Accepter un défi en attente\n"
                                "REFUSER - Refuser un défi en attente\n"
                                "JOUER <0-5> - Jouer un coup (lors d'une partie)\n"
                                "BIO <texte> - Écrire votre bio\n"
                                "INFO <pseudo> - Voir pseudo et bio d'un joueur\n"
                                "AJOUTERAMI <pseudo> - Ajouter <pseudo> à votre liste d'amis\n"
                                "CONSULTERAMI - Afficher votre liste d'amis\n"
                                "OBSERVER <id_partie> - Observer une partie en cours\n"
                                "QUITTEROBS - Quitter l'observation d'une partie\n"
                                "MSG <pseudo> <message> - Envoyer un message privé à <pseudo>\n");
                    return;
                }
            }
        }
    }
    pthread_mutex_unlock(&mutex_parties);
    envoyer_message(joueur->fd, "Vous n'observez aucune partie.\n");
    return;
}

// ----------------- Chat privé
// message attendu: débutant par "<pseudo> <texte...>" (ex: appel depuis menu: gerer_chat_prive(joueur, buffer+4))
void gerer_chat_prive(joueur_t *emetteur, char *message)
{
    char pseudo_cible[MAX_PSEUDO_LEN];

    // extraire le pseudo cible
    if (sscanf(message, "%31s", pseudo_cible) != 1)
    {
        envoyer_message(emetteur->fd, "Format: MSG <pseudo> <message>\n");
        return;
    }

    // trouver le texte après le pseudo
    char *pos = strstr(message, pseudo_cible);
    if (!pos)
    {
        envoyer_message(emetteur->fd, "Format invalide.\n");
        return;
    }
    pos += strlen(pseudo_cible);
    while (*pos == ' ')
        pos++;
    if (*pos == '\0')
    {
        envoyer_message(emetteur->fd, "Format: MSG <pseudo> <message>\n");
        return;
    }

    pthread_mutex_lock(&mutex_joueurs);
    joueur_t *dest = trouver_joueur_par_pseudo(pseudo_cible);
    if (!dest || !dest->en_ligne || dest->fd == -1)
    {
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(emetteur->fd, "Joueur non trouvé ou hors ligne.\n");
        return;
    }

    // construire et envoyer le message privé
    char buf[TAILLE_BUFFER];
    snprintf(buf, sizeof(buf), "[%s -> vous] %s\n", emetteur->pseudo, pos);
    envoyer_message(dest->fd, buf);
    pthread_mutex_unlock(&mutex_joueurs);

    // accusé au sender
    envoyer_message(emetteur->fd, "Message privé envoyé.\n");
}

// ----------------- Gérer la bio d'un joueur
// buffer attendu: "BIO <texte>" ou on peut passer directement le texte
void gerer_bio(joueur_t *joueur, char *buffer)
{
    char *texte = buffer;
    if (strncmp(buffer, "BIO ", 4) == 0)
        texte = buffer + 4;
    while (*texte == ' ')
        texte++;

    if (*texte == '\0')
    {
        envoyer_message(joueur->fd, "Format: BIO <texte>\n");
        return;
    }

    pthread_mutex_lock(&mutex_joueurs);
    strncpy(joueur->bio, texte, MAX_BIO_LEN - 1);
    joueur->bio[MAX_BIO_LEN - 1] = '\0';
    pthread_mutex_unlock(&mutex_joueurs);

    envoyer_message(joueur->fd, "Bio mise à jour.\n");
}

// ----------------- Gérer l'affichage d'info d'un joueur (pseudo + bio)
void gerer_info(joueur_t *joueur, char *buffer)
{
    char pseudo_cible[MAX_PSEUDO_LEN];
    if (sscanf(buffer, "INFO %31s", pseudo_cible) != 1)
    {
        envoyer_message(joueur->fd, "Format: INFO <pseudo>\n");
        return;
    }

    pthread_mutex_lock(&mutex_joueurs);
    joueur_t *dest = trouver_joueur_par_pseudo(pseudo_cible);
    if (!dest)
    {
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Joueur non trouvé.\n");
        return;
    }

    char buf[TAILLE_BUFFER];
    snprintf(buf, sizeof(buf), "Pseudo: %s\nBio: %s\nStatut: %s\n",
             dest->pseudo,
             (dest->bio[0] != '\0') ? dest->bio : "(aucune bio)",
             dest->en_ligne ? "en ligne" : "hors ligne");
    pthread_mutex_unlock(&mutex_joueurs);

    envoyer_message(joueur->fd, buf);
}

// ----------------- Gérer l'ajout d'un ami
// Format attendu: "AJOUTERAMI <pseudo>"
void gerer_ajouter_ami(joueur_t *joueur, char *buffer)
{
    char target[MAX_PSEUDO_LEN];
    if (sscanf(buffer, "AJOUTERAMI %31s", target) != 1)
    {
        envoyer_message(joueur->fd, "Format: AJOUTERAMI <pseudo>\n");
        return;
    }

    // ne pas s'ajouter soi-même
    if (strcmp(target, joueur->pseudo) == 0)
    {
        envoyer_message(joueur->fd, "Vous ne pouvez pas vous ajouter vous-même en ami.\n");
        return;
    }

    pthread_mutex_lock(&mutex_joueurs);

    // vérifier que la cible existe
    joueur_t *dest = trouver_joueur_par_pseudo(target);
    if (!dest)
    {
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Joueur introuvable.\n");
        return;
    }

    // vérifier doublon
    for (int i = 0; i < joueur->nb_amis; i++)
    {
        if (strcmp(joueur->amis[i], target) == 0)
        {
            pthread_mutex_unlock(&mutex_joueurs);
            envoyer_message(joueur->fd, "Ce joueur est déjà dans votre liste d'amis.\n");
            return;
        }
    }

    if (joueur->nb_amis >= MAX_AMIS)
    {
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Votre liste d'amis est pleine.\n");
        return;
    }

    // on l'ajoute enfin apres toutes les petites verifs
    snprintf(joueur->amis[joueur->nb_amis], MAX_PSEUDO_LEN, "%s", target);
    joueur->nb_amis++;

    pthread_mutex_unlock(&mutex_joueurs);

    char buf[128];
    snprintf(buf, sizeof(buf), "%s ajouté à votre liste d'amis.\n", target);
    envoyer_message(joueur->fd, buf);
}

// ----------------- Gérer la consultation de la liste d'amis
// Affiche la liste d'amis du joueur avec leur statut en ligne/hors-ligne
void gerer_consulter_ami(joueur_t *joueur)
{
    pthread_mutex_lock(&mutex_joueurs);
    if (joueur->nb_amis == 0)
    {
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Vous n'avez aucun ami dans votre liste.\n");
        return;
    }

    char out[TAILLE_BUFFER];
    int pos = 0;
    pos += snprintf(out + pos, sizeof(out) - pos, "Vos amis (%d):\n", joueur->nb_amis);
    for (int i = 0; i < joueur->nb_amis && pos < (int)sizeof(out) - 1; i++)
    {
        const char *nom = joueur->amis[i];
        joueur_t *j = trouver_joueur_par_pseudo(nom);
        const char *statut = (j && j->en_ligne) ? "en ligne" : "hors ligne";
        pos += snprintf(out + pos, sizeof(out) - pos, " - %s (%s)\n", nom, statut);
    }
    pthread_mutex_unlock(&mutex_joueurs);

    envoyer_message(joueur->fd, out);
}

//---- -----------LA COMMANDE SECRETE shrek
void shrek(joueur_t *joueur, char *buffer)
{
    char pseudo_destinataire[MAX_PSEUDO_LEN];
    const char *shrek = "   ⢀⡴⠑⡄⠀⠀⠀⠀⠀⠀⠀⣀⣀⣤⣤⣤⣀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠸⡇⠀⠿⡀⠀⠀⠀⣀⡴⢿⣿⣿⣿⣿⣿⣿⣿⣷⣦⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠀⠀⠀⠑⢄⣠⠾⠁⣀⣄⡈⠙⣿⣿⣿⣿⣿⣿⣿⣿⣆⠀⠀⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠀⠀⠀⢀⡀⠁⠀⠀⠈⠙⠛⠂⠈⣿⣿⣿⣿⣿⠿⡿⢿⣆⠀⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠀⠀⢀⡾⣁⣀⠀⠴⠂⠙⣗⡀⠀⢻⣿⣿⠭⢤⣴⣦⣤⣹⠀⠀⠀⢀⢴⣶⣆ \n"
                        "   ⠀⠀⢀⣾⣿⣿⣿⣷⣮⣽⣾⣿⣥⣴⣿⣿⡿⢂⠔⢚⡿⢿⣿⣦⣴⣾⠁⠸⣼⡿ \n"
                        "   ⠀⢀⡞⠁⠙⠻⠿⠟⠉⠀⠛⢹⣿⣿⣿⣿⣿⣌⢤⣼⣿⣾⣿⡟⠉⠀⠀⠀⠀⠀ \n"
                        "   ⠀⣾⣷⣶⠇⠀⠀⣤⣄⣀⡀⠈⠻⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠉⠈⠉⠀⠀⢦⡈⢻⣿⣿⣿⣶⣶⣶⣶⣤⣽⡹⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠀⠀⠀⠀⠀⠀⠉⠲⣽⡻⢿⣿⣿⣿⣿⣿⣿⣷⣜⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣷⣶⣮⣭⣽⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠀⠀⠀⠀⠀⣀⣀⣈⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠇⠀⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠀⠀⠀⠀⠀⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠃⠀⠀⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠀⠀⠀⠀⠀⠀⠹⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠟⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀ \n"
                        "   ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠉⠛⠻⠿⠿⠿⠿⠛⠉\n";

    // Si aucun pseudo fourni, envoyer à soi-même
    if (sscanf(buffer, "SHREK %s", pseudo_destinataire) != 1)
    {
        envoyer_message(joueur->fd, shrek);
        return;
    }

    // Chercher le destinataire
    pthread_mutex_lock(&mutex_joueurs);
    joueur_t *destinataire = trouver_joueur_par_pseudo(pseudo_destinataire);

    if (destinataire && destinataire->en_ligne)
    {
        // Envoyer au destinataire
        envoyer_message(destinataire->fd, shrek);
        pthread_mutex_unlock(&mutex_joueurs);
        envoyer_message(joueur->fd, "Shrek envoyé avec succès!\n");
    }
    else
    {
        pthread_mutex_unlock(&mutex_joueurs);
        // Joueur non trouvé ou hors ligne, envoyer à soi-même
        envoyer_message(joueur->fd, "Joueur non trouvé ou hors ligne. C'est toi qui prends le shrek:\n");
        envoyer_message(joueur->fd, shrek);
    }
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

        if (strcmp(commande, "DECO") == 0)
        {
            gerer_deconnexion(joueur);
        }
        else if (strcmp(commande, "HELP") == 0)
        {
            envoyer_message(joueur->fd, "Commandes disponibles:\n"
                                "DECO - Se déconnecter\n"
                                "HELP - Afficher cette aide\n"
                                "LISTEJ - Lister les joueurs en ligne\n"
                                "LISTEP - Lister les parties en cours\n"
                                "DEFIPU <pseudo> - Défier un joueur sur une partie publique\n"
                                "DEFIPR <pseudo> - Défier un joueur sur une partie privée\n"
                                "ACCEPTER - Accepter un défi en attente\n"
                                "REFUSER - Refuser un défi en attente\n"
                                "JOUER <0-5> - Jouer un coup (lors d'une partie)\n"
                                "BIO <texte> - Écrire votre bio\n"
                                "INFO <pseudo> - Voir pseudo et bio d'un joueur\n"
                                "AJOUTERAMI <pseudo> - Ajouter <pseudo> à votre liste d'amis\n"
                                "CONSULTERAMI - Afficher votre liste d'amis\n"
                                "OBSERVER <id_partie> - Observer une partie en cours\n"
                                "QUITTEROBS - Quitter l'observation d'une partie\n"
                                "MSG <pseudo> <message> - Envoyer un message privé à <pseudo>\n");
        }
        else if (strcmp(commande, "LISTEJ") == 0)
        {
            afficher_joueurs_en_ligne(joueur);
        }
        else if (strcmp(commande, "LISTEP") == 0)
        {
            afficher_parties_en_cours(joueur);
        }
        else if (strcmp(commande, "DEFIPU") == 0)
        {
            gerer_defi_publique(joueur, buffer);
        }
        else if (strcmp(commande, "DEFIPR") == 0)
        {
            gerer_defi_prive(joueur, buffer);
        }
        else if (strcmp(commande, "ACCEPTER") == 0)
        {
            gerer_accepter(joueur);
        }
        else if (strcmp(commande, "REFUSER") == 0)
        {
            gerer_refuser(joueur);
        }
        else if (strcmp(commande, "BIO") == 0)
        {
            if (len <= 4)
            {
                envoyer_message(joueur->fd, "Format: BIO <texte>\n");
            }
            else
            {
                gerer_bio(joueur, buffer);
            }
        }
        else if (strcmp(commande, "INFO") == 0)
        {
            if (len <= 5)
            {
                envoyer_message(joueur->fd, "Format: INFO <pseudo>\n");
            }
            else
            {
                gerer_info(joueur, buffer);
            }
        }
        else if (strcmp(commande, "AJOUTERAMI") == 0)
        {
            // Format: AJOUTERAMI <pseudo>
            char target[MAX_PSEUDO_LEN];
            if (sscanf(buffer, "AJOUTERAMI %31s", target) != 1)
            {
                envoyer_message(joueur->fd, "Format: AJOUTERAMI <pseudo>\n");
            }
            else
            {
                gerer_ajouter_ami(joueur, buffer);
            }
        }
        else if (strcmp(commande, "CONSULTERAMI") == 0)
        {
            gerer_consulter_ami(joueur);
        }
        else if (strcmp(commande, "JOUER") == 0)
        {
            int maison;
            if (sscanf(buffer, "JOUER %d", &maison) == 1)
            {
                jouer_coup(joueur, maison);
            }
            else
            {
                envoyer_message(joueur->fd, "Format invalide. Utiliser: JOUER <0-5>\n");
            }
        }
        else if (strcmp(commande, "OBSERVER") == 0)
        {
            observer_partie(joueur, buffer);
        }
        else if (strcmp(commande, "QUITTEROBS") == 0)
        {
            quitter_observation(joueur);
        }
        else if (strcmp(commande, "MSG") == 0)
        {
            // appeler le chat privé en passant la partie après "MSG "
            // vérifier qu'il y a bien quelque chose après
            if (len <= 4)
            {
                envoyer_message(joueur->fd, "Format: MSG <pseudo> <message>\n");
            }
            else
            {
                gerer_chat_prive(joueur, buffer + 4);
            }
        }
        else if (strcmp(commande, "SHREK") == 0)
        {
            shrek(joueur, buffer);
        }

        else
        {
            envoyer_message(joueur->fd, "Commande inconnue. Tapez HELP pour l'aide.\n");
        }

        memset(buffer, 0, sizeof(buffer));
    }
}

// ----------------- Main pour initialiser le serveur et accepter les connexions
// a la suite du main les sockets clients sont dans des threads séparés
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
        joueurs[i].nb_amis = 0;
        for (int k = 0; k < MAX_AMIS; k++)
            joueurs[i].amis[k][0] = '\0';
        joueurs[i].en_ligne = false;
        joueurs[i].id_partie = -1;
    }
    // Initialiser le tableau de parties
    for (int i = 0; i < MAX_NB_PARTIES; i++)
    {
        parties[i].en_cours = false;
        parties[i].joueur1 = NULL;
        parties[i].joueur2 = NULL;
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

        // c'est ici qu'on crée le thread pour gérer le client et qu'on appelle gerer_client
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