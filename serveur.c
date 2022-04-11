#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

int dSC = 0;
int dS2C = 0;
int dS = 0;
int continu = 1;

void arret(int sig) {
  continu = 0;
  if(-1== shutdown(dSC, 2)) {
    perror("Erreur shutdown dSC");
    exit(1);
  }
  if(-1== shutdown(dS2C, 2)) {
    perror("Erreur shutdown dS2C");
    exit(1);
  }
  if(-1 == shutdown(dS, 2)) {
    perror("Erreur shutdown dS");
    exit(1);
  }
  printf("\nArrêt\n");
  exit(0);
}

void accept_client(int ordre, int dS, struct sockaddr_in* aC, socklen_t lg) {
  if(ordre != 2) {
    dSC = accept(dS, (struct sockaddr*) aC,&lg) ;
    if(dSC == -1) {
      perror("Erreur accept");
      exit(1);
    }
    printf("Client 1 Connecté\n");
  }
  else {
    dS2C = accept(dS, (struct sockaddr*) aC,&lg) ;
    if(dS2C == -1) {
      perror("Erreur accept");
      exit(1);
    }
    printf("Client 2 Connecté\n");
  }
}

int main(int argc, char *argv[]) {
  
  if(argc != 2){
    printf("Lancement : ./client port\n");
    exit(1);
  }

  const int port = atoi(argv[1]);

  dS = socket(PF_INET, SOCK_STREAM, 0);
  if(dS == -1) {
    perror("Erreur socket");
    exit(1);
  }
  printf("Socket Créé\n");


  struct sockaddr_in ad;
  ad.sin_family = AF_INET;
  ad.sin_addr.s_addr = INADDR_ANY;
  ad.sin_port = htons(port);
  if(-1 == bind(dS, (struct sockaddr*)&ad, sizeof(ad))) {
    perror("Erreur bind");
    exit(1);
  }
  printf("Socket 1 Nommé\n");

  if(-1 == listen(dS, 7)) {
    perror("Erreur listen");
    exit(1);
  }
  printf("Mode 1 écoute\n");

  struct sockaddr_in aC1 ;
  socklen_t lg1 = sizeof(struct sockaddr_in) ;
  accept_client(1, dS, &aC1, lg1);

  struct sockaddr_in aC2 ;
  socklen_t lg2 = sizeof(struct sockaddr_in) ;
  accept_client(2, dS, &aC2, lg2);

  int taille = 20;
  char msg1[taille];
  char msg2[taille];
  int continu = 1; // Booléen
  int reconnect = 0; // Booléen
  int client1 = 1;
  int client2 = 1;
  signal(SIGINT, arret);
  do {
    if(reconnect == 1) {
      strcpy(msg1, "");
      strcpy(msg2, "");
      accept_client(1, dS, &aC1, lg1);
      accept_client(2, dS, &aC2, lg2);
      reconnect = 0;
      printf("hallo");
    }
    //RECV FROM 1
    if(!reconnect) {
      client1 = recv(dSC, msg1, taille*sizeof(char), MSG_NOSIGNAL);
      if(-1 == client1) {
        perror("Erreur recv client 1");exit(1);
      }
      //Client 1 déconnecté
      else if(0 == client1)
        reconnect = 1;
      else
        printf("Message reçu de client 1: %s", msg1);
    }
    
    //SEND TO 2
    if(!reconnect) {
      client2 = send(dS2C, msg1, taille*sizeof(char), MSG_NOSIGNAL);
      if(-1 == client2) {
        perror("Erreur send au client 2");exit(1);
      }
      //Client 2 déconnecté
      else if(0 == client2)
        reconnect = 1;
    }

    //RECV FROM 2
    if(!reconnect) {
      client2 = recv(dS2C, msg2, taille*sizeof(char), MSG_NOSIGNAL);
      if(-1 == client2) {
        perror("Erreur recv client 2");exit(1);
      }
      //Client 2 déconnecté
      else if(0 == client2)
        reconnect = 1;
      else
        printf("Message reçu de client 2 : %s", msg2);
    }
    
    //SEND TO 1
    if(!reconnect) {
      client1 = send(dSC, msg2, taille*sizeof(char), MSG_NOSIGNAL);
      if(-1 == client1) {
        perror("Erreur send au client 1");exit(1);
      }
      //Client 1 déconnecté
      else if(0 == client1)
        reconnect = 1;
    }

  } while(continu);

  printf("Fin du programme\n");
  return 0;
}