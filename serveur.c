#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  
  if(argc != 2){
    printf("Lancement : ./client port\n");
    exit(1);
  }

  const int port = atoi(argv[1]);

  printf("Début programme\n");

  int dS1 = socket(PF_INET, SOCK_STREAM, 0);
  if(dS1 == -1) {
    perror("Erreur socket");
    exit(1);
  }
  printf("Socket  1 Créé\n");


  struct sockaddr_in ad1;
  ad1.sin_family = AF_INET;
  ad1.sin_addr.s_addr = INADDR_ANY;
  ad1.sin_port = htons(port);
  if(-1 == bind(dS1, (struct sockaddr*)&ad1, sizeof(ad1))) {
    perror("Erreur bind");
    exit(1);
  }
  printf("Socket 1 Nommé\n");

  if(-1 == listen(dS1, 7)) {
    perror("Erreur listen");
    exit(1);
  }
  printf("Mode 1 écoute\n");

  struct sockaddr_in aC1 ;
  socklen_t lg1 = sizeof(struct sockaddr_in) ;
  int dS1C = accept(dS1, (struct sockaddr*) &aC1,&lg1) ;
  if(dS1C == -1) {
    perror("Erreur accept");
    exit(1);
  }
  printf("Client 1 Connecté\n");

  struct sockaddr_in aC2 ;
  socklen_t lg2 = sizeof(struct sockaddr_in) ;
  int dS2C = accept(dS1, (struct sockaddr*) &aC2,&lg2) ;
  if(dS2C == -1) {
    perror("Erreur accept");
    exit(1);
  }
  printf("Client 2 Connecté\n");

  

  char arret[2]; // q pour arrêter
  do {
    //Client 1 réception

    char * msg1 = (char *)malloc(sizeof(char)*20) ;
    if(-1 == recv(dS1C, msg1, 20 * sizeof(char), 0)) {
      perror("Erreur recv client 1 ");
      exit(1);
    }
    printf("Message reçu de client 1: %s\n", msg1) ;
    arret[0] = msg1[0];
    
    //Envoie msg1 à Client 2
    if(-1 == send(dS2C, msg1, sizeof(char)*20, 0)) {
      perror("Erreur send au client 2");
      exit(1);
    }
    free(msg1);
    
    //Client 2 réception

    char * msg2 = (char *)malloc(sizeof(char)*20) ;
    if(-1 == recv(dS2C, msg2, 20 * sizeof(char), 0)) {
      perror("Erreur recv client 2 ");
      exit(1);
    }
    printf("Message reçu de client 2 : %s\n", msg2) ;
    arret[1] = msg2[0];
    
    //Envoie msg2 à Client 1
    if(-1 == send(dS1C, msg2, sizeof(char)*20, 0)) {
      perror("Erreur send au lient 1");
      exit(1);
    }
    free(msg2);

  } while(!(arret[0] == "fin" || arret[1] == "fin"));
  printf("Message Envoyé 2\n");
  if(-1== shutdown(dS1C, 2)) {
    perror("Erreur shutdown dS1C");
    exit(1);
  }
  if(-1== shutdown(dS2C, 2)) {
    perror("Erreur shutdown dS2C");
    exit(1);
  }
  if(-1 == shutdown(dS1, 2)) {
    perror("Erreur shutdown dS1");
    exit(1);
  }
  printf("Fin du programme\n");
}