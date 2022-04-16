#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>

int MAX_CLIENTS = 20;
int SIZE_MESSAGE = 20;

int dS;
pthread_mutex_t mutex;
int nb_thread;
pthread_t *thread; //Liste des clients

//Structure envoyé en paramètre des threads
struct params {
  int dSC;
  int numero;
  int * clients;
  int * nbClients;
};

/**
 * @brief Ferme le socket serveur
 * 
 * @param dS 
 */
void stopServeur(int dS) {
  //Ferme les threads des clients
  for (int i=0;i<nb_thread;i++) 
    pthread_cancel(thread[i]);
  //pthread_mutex_destroy(&mutex);
  free(thread);

  if(-1 == shutdown(dS, 2)) {
    perror("Erreur shutdown serveur");
    exit(1);
  }
  puts("Arrêt serveur");
}
/**
 * @brief Fonction déclenchée lors d'un contrôle C
 * 
 */
void arret() {
  stopServeur(dS);
  exit(EXIT_SUCCESS);
}
/**
 * @brief Fonction des threads clients, elle gère la réception d'un message envoyé par le client au serveur,
 *        et envoie ce message aux autres clients
 * @param parametres 
 * @return void* 
 */
void* client(void * parametres) {
  struct params* p = (struct params*) parametres;
  char msg[SIZE_MESSAGE];
  
  // Tant que l'utilisateur n'a pas envoyé fin
  do {
    int r = recv(p->dSC, msg, SIZE_MESSAGE*sizeof(char), 0);
    if(-1 == r) {
      perror("Erreur recv client");exit(1);
    }
    int nb = *(p->nbClients);
    for(int i = 0; i<nb; i++) {
      if(p->numero != i && p->clients[i] != -1) {
        int s = send(p->clients[i], msg, strlen(msg)+1, 0);
        if(-1 == s) {
          perror("Erreur send client");exit(1);
        }
      }
    }
  } while(strcmp(msg, "fin\n") != 0);

  // Change la valeur dans la liste des clients à -1 pour indiquer qu'il n'est plus connecté au serveur
  p->clients[p->numero] = -1;
  if(-1 == close(p->dSC)) { 
    perror("Erreur close client");exit(1);
  }
  pthread_exit(0);
}

/**
 * @brief Main
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */
int main(int argc, char *argv[]) {
  
  if(argc != 2){
    puts("Lancement : ./client port");
    exit(1);
  }

  const int port = atoi(argv[1]);

  // Lancement du serveur
  dS = socket(PF_INET, SOCK_STREAM, 0);
  if(dS == -1) {
    perror("Erreur socket");
    exit(1);
  }
  puts("Socket Créé");

  struct sockaddr_in ad;
  ad.sin_family = AF_INET;
  ad.sin_addr.s_addr = INADDR_ANY;
  ad.sin_port = htons(port);
  if(-1 == bind(dS, (struct sockaddr*)&ad, sizeof(ad))) {
    perror("Erreur bind");exit(1);
  }
  puts("Socket 1 Nommé");

  if(-1 == listen(dS, 7)) {
    perror("Erreur listen");exit(1);
  }
  puts("Mode 1 écoute");

  // Liste qui contiendra les sockets des clients
  nb_thread = 0;
  int clients[MAX_CLIENTS];
  thread = (pthread_t*)malloc(MAX_CLIENTS*sizeof(pthread_t));

  signal(SIGINT, arret);

  // On attend qu'un nouveau client veuille se connecter
  while(1) {
    // Tant que le nombre max de clients n'est pas atteint, on va attendre une connexion
    if(nb_thread < MAX_CLIENTS) {
      struct sockaddr_in aC ;
      socklen_t lg = sizeof(struct sockaddr_in);
      int dSC = accept(dS, (struct sockaddr*)&aC,&lg) ;
      if(dSC == -1) {
        perror("Erreur accept");exit(1);
      }

      // On lance un thread pour chaque client, avec sa socket, son numéro de client, et la liste des clients
      puts("Client Connecté");
      clients[nb_thread] = dSC;
      struct params* p = (struct params*) malloc(sizeof(struct params));
      p->dSC = dSC;
      p->numero = nb_thread;
      p->clients = clients;
      p->nbClients = &nb_thread;
      pthread_create(&thread[nb_thread++], NULL, client, (void*)p);
    }
  }

  stopServeur(dS);
  exit(EXIT_SUCCESS);
}