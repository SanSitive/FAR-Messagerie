#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {

  if(argc != 3 && argc != 4){
    printf("Lancement : ./client ip port (ordre)\n");
    exit(1);
  }

  const int port = atoi(argv[2]);
  int ordre = 1;
  if(argc == 4)
    ordre = atoi(argv[3]);

  int dS = socket(PF_INET, SOCK_STREAM, 0);
  if(dS == -1) {
    perror("Erreur socket");
    exit(1);
  }
  printf("Socket Créé\n");

  struct sockaddr_in aS;
  aS.sin_family = AF_INET;
  inet_pton(AF_INET, argv[1], &(aS.sin_addr));
  aS.sin_port = htons(port);
  socklen_t lgA = sizeof(struct sockaddr_in);
  if(-1 == connect(dS, (struct sockaddr *) &aS, lgA)) {
    perror("Erreur connect");
    exit(1);
  }
  printf("Socket Connecté\n");

  int taille = 20;
  char m[taille];
  char reception[taille];

  if(ordre == 2) {
    if(-1 == recv(dS, reception, sizeof(char)*20, 0)) {
      perror("Erreur recv 2");
      exit(1);
    }
    printf("Réponse reçue : %s\n", reception);
  }

  do {
    printf("Entrer une chaîne de caractère\n");
    fgets(m, taille, stdin);
    //SEND
    if(-1 == send(dS, m, strlen(m)+1, 0)) {
      perror("Erreur send");
      exit(1);
    }
    printf("Message Envoyé\n");

    //RECV
    if(strcmp(m, "fin\n")!=0) {
      if(-1 == recv(dS, reception, sizeof(char)*20, 0)) {
        perror("Erreur recv");
        exit(1);
      }
      printf("\nRéponse reçue : %s\n", reception);
    }

  } while(strcmp(m, "fin\n")!=0 && strcmp(reception, "fin\n")!=0);

  if(-1 == shutdown(dS,2)) {
    perror("Erreur shutdown dS");
    exit(1);
  }
  printf("Fin du programme\n");
  return 0;
}