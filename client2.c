#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {

  if(argc != 3){
    printf("Lancement : ./client ip port\n");
    exit(1);
  }

  const int port = atoi(argv[2]);

  printf("Début programme\n");

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

  char * m = (char *)malloc(sizeof(char)*20);
  char * reception = (char *)malloc(sizeof(char)*20);

  do { // q pour arrêter
    if(-1 == recv(dS, reception, sizeof(char)*20, 0)) {
      perror("Erreur recv 2");
      exit(1);
    }
    printf("Réponse reçue 2 : %s\n", reception);

    printf("Entrer une chaîne de caractère\n");
    scanf(" %s", m);

    //SEND STRING
    if(-1 == send(dS, m, strlen(m)+1, 0)) {
      perror("Erreur send 2");
      exit(1);
    }
    printf("Message Envoyé 1\n");

  } while(*m != 'q');
  free(m);

  if(-1 == shutdown(dS,2)) {
    perror("Erreur shutdown dS");
    exit(1);
  }
  printf("Fin du programme\n");
}