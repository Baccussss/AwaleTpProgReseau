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

#include "awale.h" // contient Awale + afficher_interface_jeu

#define BACKLOG 5

// envoi robuste (gère les envois partiels)
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
static void send_str(int fd, const char *s) { (void)send_all(fd, s, strlen(s)); }

// lit une ligne terminée par '\n'
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
      continue;
    buf[i++] = c;
    if (c == '\n')
      break;
  }
  buf[i] = '\0';
  return (int)i;
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    return 1;
  }

  signal(SIGPIPE, SIG_IGN); // évite un crash si un client coupe pendant send()

  // --- socket d'écoute ---
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

  // --- accepter exactement deux joueurs ---
  int players[2] = {-1, -1};
  struct sockaddr_in cli_addr;
  socklen_t clilen = sizeof(cli_addr);
  for (int i = 0; i < 2; ++i)
  {
    clilen = sizeof(cli_addr);
    players[i] = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
    if (players[i] < 0)
    {
      perror("accept");
      return 1;
    }
    printf("Joueur %d connecté depuis %s\n", i + 1, inet_ntoa(cli_addr.sin_addr));
    char hello[160];
    snprintf(hello, sizeof(hello),
             "Bienvenue ! Vous êtes Joueur %d.\nEn attente de l'autre joueur...\n\n", i + 1);
    send_str(players[i], hello);
  }

  send_str(players[0], "Les deux joueurs sont présents. Vous êtes Joueur 1.\n");
  send_str(players[1], "Les deux joueurs sont présents. Vous êtes Joueur 2.\n");

  // --- état du jeu ---
  Awale g;
  awale_init(&g);

  // --- envoyer l'UI POV à chacun (début) ---
  char ui0[1024], ui1[1024];
  afficher_interface_jeu(ui0, sizeof(ui0), g.board, g.score, g.current_player, 0); // POV J1
  afficher_interface_jeu(ui1, sizeof(ui1), g.board, g.score, g.current_player, 1); // POV J2
  send_str(players[0], ui0);
  send_str(players[1], ui1);

  // --- boucle de jeu ---
  while (1)
  {
    int p = g.current_player; // 0 -> J1 ; 1 -> J2
    int fd_curr = players[p];
    int fd_wait = players[1 - p];

    send_str(fd_curr, "Entrez un nombre (0-5) puis Entrée:\n");

    // lire la saisie du joueur courant
    char line[128];
    int r = recv_line(fd_curr, line, sizeof(line));
    if (r <= 0)
    {
      send_str(fd_wait, "L'autre joueur s'est déconnecté. Fin de partie.\n");
      break;
    }

    // parser l'entier
    char *endptr = NULL;
    int h = (int)strtol(line, &endptr, 10);
    if (endptr == line || h < 0 || h >= HOUSES_PER_SIDE)
    {
      send_str(fd_curr, "Entrée invalide. Tapez un entier entre 0 et 5.\n");
      // réafficher pour rester synchro
      afficher_interface_jeu(ui0, sizeof(ui0), g.board, g.score, g.current_player, 0);
      afficher_interface_jeu(ui1, sizeof(ui1), g.board, g.score, g.current_player, 1);
      send_str(players[0], ui0);
      send_str(players[1], ui1);
      continue;
    }

    // tenter le coup (index relatif au joueur courant)
    if (!awale_move(&g, h))
    {
      send_str(fd_curr, "Coup invalide (maison vide / règle). Réessayez.\n");
      afficher_interface_jeu(ui0, sizeof(ui0), g.board, g.score, g.current_player, 0);
      afficher_interface_jeu(ui1, sizeof(ui1), g.board, g.score, g.current_player, 1);
      send_str(players[0], ui0);
      send_str(players[1], ui1);
      continue;
    }

    // coup OK → diffuser l'état POV aux deux
    afficher_interface_jeu(ui0, sizeof(ui0), g.board, g.score, g.current_player, 0);
    afficher_interface_jeu(ui1, sizeof(ui1), g.board, g.score, g.current_player, 1);
    send_str(players[0], ui0);
    send_str(players[1], ui1);

    // fin de partie ? (ta version simple — à remplacer si tu implémentes famine/indétermination)
    if (awale_is_game_over(&g))
    {
      char endmsg[256];
      snprintf(endmsg, sizeof(endmsg),
               "Partie terminée. Scores : J1=%d, J2=%d\n%s\n",
               g.score[0], g.score[1],
               (g.score[0] > g.score[1]) ? "J1 gagne !" : (g.score[1] > g->score[0]) ? "J2 gagne !"
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