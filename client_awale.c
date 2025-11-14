// client_awale.c
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <ip_server> <port>\n", argv[0]);
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("connect");
        return 1;
    }
    printf("Connecté au serveur %s:%s\n", argv[1], argv[2]);

    // On veut pouvoir lire du clavier ET du socket → select()
    while (1)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(sockfd, &rfds);
        int nfds = (sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO) + 1;

        int ready = select(nfds, &rfds, NULL, NULL, NULL);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        // Données du serveur ?
        if (FD_ISSET(sockfd, &rfds))
        {
            char buf[512];
            ssize_t r = recv(sockfd, buf, sizeof(buf) - 1, 0);
            if (r <= 0)
            {
                printf("Déconnecté du serveur.\n");
                break;
            }
            buf[r] = '\0';
            fputs(buf, stdout);
            fflush(stdout);
        }

        // Saisie utilisateur ?
        if (FD_ISSET(STDIN_FILENO, &rfds))
        {
            char line[128];
            if (!fgets(line, sizeof(line), stdin))
            {
                // EOF (Ctrl+D)
                break;
            }
            // Envoi tel quel (ex: "3\n")
            send(sockfd, line, strlen(line), 0);
        }
    }

    close(sockfd);
    return 0;
}