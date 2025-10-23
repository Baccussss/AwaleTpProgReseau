/* Serveur sockets TCP
 * affichage de ce qui arrive sur la socket
 *    socket_server port (port > 1024 sauf root)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <signal.h>
#include <ifaddrs.h>
#include <net/if.h>

int main(int argc, char** argv )
{ char datas[] = "hello\n";
  int    sockfd,newsockfd,clilen,chilpid,ok,nleft,nbwriten;
  char c;
  struct sockaddr_in cli_addr,serv_addr;
  char serverIpAdress[INET_ADDRSTRLEN] = "127.0.0.1";
  

  if (argc!=2) {printf ("usage: socket_server port\n");exit(0);}

  // Va chercher l'adresse IP de la machine pour l'afficher
  struct ifaddrs *ifaddr = NULL, *ifa = NULL;
  if (getifaddrs(&ifaddr) == 0) {
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr) continue;
      if (ifa->ifa_addr->sa_family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
        struct sockaddr_in *sa = (struct sockaddr_in*)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, serverIpAdress, sizeof(serverIpAdress));
        break; // first non-loopback IPv4
      }
    }
    freeifaddrs(ifaddr);
  }

  printf ("server starting at [%s] port [%s]\n",serverIpAdress, argv[1]);

  /* ouverture du socket */
  sockfd = socket (AF_INET,SOCK_STREAM,0);
  if (sockfd<0) {printf ("impossible d'ouvrir le socket\n");exit(0);}

  /* initialisation des parametres */
  bzero((char*) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family       = AF_INET;
  serv_addr.sin_addr.s_addr  = htonl(INADDR_ANY);
  serv_addr.sin_port         = htons(atoi(argv[1]));

  /* effecture le bind */
  if (bind(sockfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr))<0)
     {printf ("impossible de faire le bind\n");exit(0);}

  // ---------------- 0: Met le socket en écoute (max 5 client dans file d'attente)
  /* petit initialisation */
  listen(sockfd,5);
     
  /* attend la connection d'un client */
  clilen = sizeof (cli_addr);
  
  signal(SIGCHLD,SIG_IGN); //signal de tuer les processus zombies
  int pid;

  /* ---------------- 1: Boucle d'acceptation
  accept permet d'attendre l'arrivé d'un client
  à l'accept l'addresse client est rempli (cli_addr) et son IP est affiché avec inet_ntoa(cli_addr.sin_addr)

  */
  while (1)
	{
	 	newsockfd = accept (sockfd,(struct sockaddr*) &cli_addr, &clilen); 
		if (newsockfd<0) 
      {printf ("accept error\n"); exit(0);}
		printf ("connection accepted from [%s]\n", inet_ntoa(cli_addr.sin_addr));

    /* ------------- 2: Boucle d'acceptation
      fork() crée un processus fils pour gérer ce client.
      Fils:
        Ferme le socket d’écoute (sockfd), garde newsockfd.
        Boucle de lecture caractère par caractère:      //on affiche dans le terminal tout ce qu'on recoit
          read(newsockfd, &c, 1) jusqu’à lire exactement 1 octet.
          Si c == '\n': 
            affiche “ [IP]\n”.
          Sinon: 
            affiche le caractère tel quel.
        À la fin prévue, il ferme newsockfd et exit(0).
      Père:
        Ferme newsockfd (le socket de la session).
        Retourne dans accept() pour prendre d’autres clients en parallèle.
    */
		pid = fork();
		if (pid == 0) /* c’est le fils */
		{
			close(sockfd); /* socket inutile pour le fils */
			while(1) {  
				while (read(newsockfd,&c,1)!=1);
			 	if (c == '\n') {
          //keep what was received before newline in a msg_buffer
          char msg_buffer[1024];
          snprintf(msg_buffer, sizeof(msg_buffer), " [%s]\n", inet_ntoa(cli_addr.sin_addr));

          //print the received message with IP address
          printf("%s", msg_buffer);
          //broadcast to every connected client what was received
          for (int i = 0; i < strlen(msg_buffer); i++) {
            write(newsockfd, &msg_buffer[i], 1);
          }
			 	} else {
			 		printf("%c",c);
			 	}
			}
			close(newsockfd);
			exit(0); /* on force la terminaison du fils */
		}
		else /* c’est le pere */
		{
			close(newsockfd); /* socket inutile pour le pere */
		}
	}
	close(sockfd);
      
   /*  attention il s'agit d'une boucle infinie 
    *  le socket nn'est jamais ferme !
    */

   return 1;
 }
