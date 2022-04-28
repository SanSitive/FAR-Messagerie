#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <sys/sem.h>
#include <semaphore.h>

#define SIZE_MESSAGE 256
int dS;

/**
 * @brief Ferme le socket client
 * 
 * @param dS 
 */
void stopClient(int dS) {
  puts("Fin du client");
  if(-1 == shutdown(dS,2)) {
    perror("Erreur shutdown dS");exit(1);
  }
}

/**
 * @brief Fonction déclenchée lors d'un contrôle C
 * 
 */
void arret() {
  wait(NULL); // Tue le fils
  char m[SIZE_MESSAGE] = "@disconnect";
  if(-1 == send(dS, m, strlen(m)+1, 0)) { // Prévenir le serveur
    perror("Erreur send");exit(1);
  }
  stopClient(dS); // Fermer la socket
  exit(EXIT_SUCCESS);
}

/**
 * @brief Gère les entrées de l'utilisateur pour envoyer au serveur
 * 
 * @param dS 
 * @param taille 
 */
void pereSend(int dS) {
  char m[SIZE_MESSAGE];
  int s;
  do {
    fgets(m, SIZE_MESSAGE, stdin);
    
    if(strlen(m) > 0) {
      s = send(dS, m, strlen(m)+1, 0);
      if(-1 == s) {
        perror("Erreur send");exit(1);
      }
      // Non déconnecté
      else if(s != 0) {
        puts("Message Envoyé");
      }
    }
  } while(strcmp(m, "@d\n")!=0 && strcmp(m, "@disconnect\n")!=0 && s!=0);
}

/**
 * @brief Gère la réception des messages du serveur, et affiche sur le terminal
 * 
 * @param dS 
 * @param taille 
 */
void filsRecv(int dS) {
  char reception[SIZE_MESSAGE];
  int r;
  do {
    r = recv(dS, reception, sizeof(char)*SIZE_MESSAGE, 0);
    if(-1 == r) {
      perror("Erreur recv");exit(1);
    }
    else if(strcmp(reception, "@shutdown") == 0) {
      break;
    }
    // Non déconnecté
    else if(r != 0) {
      puts(reception);
    }
  } while(strcmp(reception, "@d\n")!=0 && strcmp(reception, "@disconnect\n")!=0 && r!=0);
}

int main(int argc, char *argv[]) {

  if(argc != 3){
    puts("Lancement : ./client ip port");
    exit(1);
  }

  const int port = atoi(argv[2]);

  //Création du client
  dS = socket(PF_INET, SOCK_STREAM, 0);
  if(dS == -1) {
    perror("Erreur socket");
    exit(1);
  }
  puts("Socket Créé");
  puts("Connexion en cours");

  struct sockaddr_in aS;
  aS.sin_family = AF_INET;
  inet_pton(AF_INET, argv[1], &(aS.sin_addr));
  aS.sin_port = htons(port);
  socklen_t lgA = sizeof(struct sockaddr_in);
  if(-1 == connect(dS, (struct sockaddr *) &aS, lgA)) {
    perror("Erreur connect");
    exit(1);
  }

  char m[SIZE_MESSAGE];

  //On attends la confirmation de connexion au serveur
  if(-1 == recv(dS, m, sizeof(char)*SIZE_MESSAGE, 0)) {
    perror("Erreur connexion au serveur");exit(1);
  }

  if(strcmp(m, "OK") == 0) {
    puts("Socket connecté");

    // Choix du pseudo
    puts("Choisissez un pseudo :");
    fgets(m, SIZE_MESSAGE, stdin);
    //Enlever \n à la fin du pseudo
    m[strcspn(m, "\n")] = 0;
    
    if(-1 == send(dS, m, strlen(m)+1, 0)) {
      perror("Erreur send Pseudo");exit(1);
    }
    if(-1 == recv(dS, m, sizeof(char)*SIZE_MESSAGE, 0)) {
      perror("Erreur recv Pseudo");exit(1);
    }

    // Si pseudo accepté
    if(strcmp(m, "OK") == 0) {
      puts("Login réussi");
      pid_t pid;
      // Fork pour que l'un gère l'envoie, l'autre la réception
      pid = fork();
      if (pid != 0) { // PERE
        signal(SIGINT, arret);
        pereSend(dS);
        kill(pid, SIGINT); //Tue le fils
        stopClient(dS);
        exit(EXIT_SUCCESS);
      }
      else { // FILS
        filsRecv(dS);
        stopClient(dS);
        kill(getppid(), SIGINT);
        exit(EXIT_SUCCESS);
      }
    }
    else if(strcmp(m, "PseudoTaken") == 0) {
      puts("Ce pseudo est déjà pris");
    }
  }
  else {
    puts("Le socket n'a pas pu se connecté au serveur");
  }

  exit(EXIT_SUCCESS);
}