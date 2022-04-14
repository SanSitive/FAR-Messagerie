#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

int dS;

/**
 * @brief Ferme la socket cliente
 * 
 * @param dS 
 */
void stopClient(int dS) {
  puts("Fin du clent");
  if(-1 == shutdown(dS,2)) {
    perror("Erreur shutdown dS");exit(1);
  }
}

/**
 * @brief Fonction déclenché lors d'un contrôle C
 * 
 */
void arret() {
  wait(NULL); // Tuer le fils
  char m[10] = "fin\n";
  if(-1 == send(dS, m, strlen(m)+1, 0)) { // Prévenir le serveur
    perror("Erreur send");exit(1);
  }
  stopClient(dS); // Fermer le socket
  exit(EXIT_SUCCESS);
}

/**
 * @brief Gère les entrées des utilisateurs pour envoyer au serveur
 * 
 * @param dS 
 * @param taille 
 */
void pereSend(int dS, int taille) {
  char m[taille];
  int s;
  do {
    puts("Entrer une chaîne de caractère");
    fgets(m, taille, stdin);
    
    if(strlen(m) > 0) {
      s = send(dS, m, strlen(m)+1, 0);
      if(-1 == s) {
        perror("Erreur send");exit(1);
      }
      // Non déconnecté
      else if(s != 0) {
        puts("Message Envoyé");
      }
      else {
        
      }
    }
  } while(strcmp(m, "fin\n")!=0 && s!=0);
}

/**
 * @brief Gère la réception des messages du serveur
 * 
 * @param dS 
 * @param taille 
 */
void filsRecv(int dS, int taille) {
  char reception[taille];
  int r;
  do {
    r = recv(dS, reception, sizeof(char)*20, 0);
    if(-1 == r) {
      perror("Erreur recv");exit(1);
    }
    // Non déconnecté
    else if(r != 0) {
      char msg[30] = "Reçu : ";
      strcat(msg, reception);
      puts(msg);
    }
  } while(strcmp(reception, "fin\n")!=0 && r!=0);
}

int main(int argc, char *argv[]) {

  if(argc != 3){
    puts("Lancement : ./client ip port");
    exit(1);
  }

  const int port = atoi(argv[2]);

  dS = socket(PF_INET, SOCK_STREAM, 0);
  if(dS == -1) {
    perror("Erreur socket");
    exit(1);
  }
  puts("Socket Créé");

  struct sockaddr_in aS;
  aS.sin_family = AF_INET;
  inet_pton(AF_INET, argv[1], &(aS.sin_addr));
  aS.sin_port = htons(port);
  socklen_t lgA = sizeof(struct sockaddr_in);
  if(-1 == connect(dS, (struct sockaddr *) &aS, lgA)) {
    perror("Erreur connect");
    exit(1);
  }
  puts("Socket Connecté");

  int taille = 20;
  pid_t pid;

  pid = fork();
	if (pid != 0) { // PERE
    signal(SIGINT, arret);
    pereSend(dS, taille);
    kill(pid, SIGINT);
    stopClient(dS);
    exit(EXIT_SUCCESS);
  }
  else { // FILS
    filsRecv(dS, taille);
    /*stopClient(dS);
    kill(getppid(), SIGINT);
    exit(EXIT_SUCCESS);*/
  }

  exit(EXIT_SUCCESS);
}