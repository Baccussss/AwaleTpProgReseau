# AwaleTpProgReseau
Pour jouer:
Faire un make
Lancer un ./server_awale <n°Port> et deux ./client_awale <IP server> <n°Port>

le ./awale crée est une relique 

## Liste des commandes 

HELP
    Affiche la liste complète des commandes disponibles.

DECO
    Se déconnecte du serveur. 

LISTEJ
    Affiche tous les joueurs actuellement en ligne,
    avec indication s’ils sont en partie ou si c’est vous.

LISTEP
    Affiche toutes les parties en cours, avec leur ID
    et les deux joueurs qui y participent.

DEFIPR <pseudo>
    Envoie un défi à un joueur pour commencer une partie.
    Impossible si vous ou lui êtes déjà en partie, ou s’il a déjà un défi en attente.
    Comme c'est une partie privée, il faut être ami avec au moins un joueur pour pouvoir l'observer.

DEFIPU
    Envoie un défi à un joueur pour commencer une partie.
    Impossible si vous ou lui êtes déjà en partie, ou s’il a déjà un défi en attente.
    Comme c'est une partie public, tout le monde peut observer la partie.

ACCEPTER
    Accepte le dernier défi que vous avez reçu et lance une partie.

REFUSER
    Refuse le défi reçu. Le joueur adverse est informé.

JOUER <0-5>
    Joue un coup dans une partie en cours (numéro de maison de 0 à 5).
    Ne fonctionne que si c’est votre tour.

BIO <texte>
    Met à jour votre bio (description personnelle affichée dans INFO).

INFO <pseudo>
    Affiche les informations d’un joueur : pseudo, bio et statut (en ligne/hors-ligne).

AJOUTERAMI <pseudo>
    Ajoute le joueur à votre liste d’amis (si existant et pas déjà ami).

CONSULTERAMI
    Affiche votre liste d’amis et leur statut (en ligne/hors-ligne).

OBSERVER <id_partie>
    Vous permet de rejoindre une partie en cours comme observateur
    (vue du plateau mise à jour à chaque coup).

QUITTEROBS
    Quitte l’observation de la partie en cours.

MSG <pseudo> <message>
    Envoie un message privé au joueur spécifié.

SHREK <pseudo>
    Commande bonus : envoie un ASCII art de Shrek. 
    C'était à un moment ou tout bugguait et on avait besoin de se détendre 
    

## Utilisation de l'IA au sein du projet 

Dans ce projet, l’IA a été utilisée comme aide à la conception et à la relecture du code, en particulier pour la création du jeu awale, gestion concurrente et la manipulation des chaînes de caractères. 

Nous voulions passer principalement notre temps sur la partie réseau du projet. L'IA nous a aidé à aller un peu plus vite sur l'algorithme du jeu d'Awale

Elle a suggéré l’utilisation de pthread_mutex_t pour protéger les tableaux globaux joueurs et parties, ainsi que le compteur nb_parties_actives, afin d’éviter les accès concurrents et les conditions de course lors des connexions, des défis et de la création/fin de partie. 

L’IA a également aidé à structurer une lecture robuste des commandes avec recv_line (lecture caractère par caractère, gestion de EINTR, prise en charge de \r\n) et à sécuriser les chaînes en C en limitant les copies (snprintf, strncpy, tailles fixes de buffers, vérification de format avec sscanf) pour réduire les risques de dépassement de tampon et de comportements indéfinis.

Enfin de manière générale, quand nous avions des bugs persistants, nous demandions à l'IA quel pouvait être le problème ! 