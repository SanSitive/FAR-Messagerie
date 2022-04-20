#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <sys/sem.h>
#include <semaphore.h>

#define MAX_CLIENTS 2
#define SIZE_MESSAGE 128
#define SIZE_HELP 243 //à changer si jamais on agrandi la taille de help.txt

int dS;
pthread_mutex_t mutex;
pthread_mutex_t mutex_help; //Mutex pour le fichier help.txt
int nb_thread;
pthread_t *thread; //Liste des threads
struct clientStruct ** clients; //Liste des clients

//Création d'un sémaphore indiquant le nombre de place restante
sem_t sem_place;

// Structure décrivant un client, envoyé dans les threads
struct clientStruct {
  int dSC; //Socket
  char * pseudo;
  int numero; //Index dans le tableau de clients
};

/**
 * @brief Ferme le socket serveur
 * 
 * @param dS 
 */
void stopServeur(int dS) {
  //Ferme les threads des clients
  pthread_mutex_lock(&mutex);
  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i] != NULL) {
      printf("%d : %d\n",i, clients[i]->dSC);
      free(clients[i]->pseudo);
      free(clients[i]);
    }
  }
  free(clients);

  for (int i=0;i<nb_thread;i++){
    pthread_cancel(thread[i]);
  }
  free(thread);
  pthread_mutex_unlock(&mutex);
  pthread_mutex_destroy(&mutex);

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
 * @brief Vérifie si un pseudo est déjà pris. Retourne 1 si oui, 0 sinon
 * 
 * @param pseudo 
 * @return int 
 */
int isPseudoTaken(char pseudo[]) {
  int res = 0;
  pthread_mutex_lock(&mutex);
  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i] != NULL) {
      if(clients[i]->pseudo != NULL) {
        if(strcmp(pseudo, clients[i]->pseudo) == 0) {
          res = 1;
          break;
        }
      }
    }
  }
  pthread_mutex_unlock(&mutex);
  return res;
}

/**
 * @brief Vérifie les informations de connexion d'un client.
 *        Retourne 1 si les informations sont bonnes, 0 sinon, ou si le pseudo est déjà pris
 * @param dSC
 * @param p
 * @return int
 */
int connexion(int dSC, struct clientStruct * p) {
  int res = 1;
  char msg[SIZE_MESSAGE];
  // Récupérer les infos
  if(-1 == recv(p->dSC, msg, SIZE_MESSAGE*sizeof(char), 0)) {
    perror("Erreur recv connexion");exit(1);
  }

  // Vérification
  if(isPseudoTaken(msg) == 1) res = 0;

  // Réponse au client
  if(res == 1) {
    char retour[20] = "OK";
    if(-1 == send(dSC, retour, 20, 0)) {
      perror("Erreur send connexion");exit(1);
    }
    pthread_mutex_lock(&mutex);
    p->pseudo = (char*)malloc(sizeof(char)*strlen(msg)+1);
    strcpy(p->pseudo, msg);
    pthread_mutex_unlock(&mutex);
  }
  else {
    char retour[20] = "PseudoTaken";
    if(-1 == send(dSC, retour, 20, 0)) {
      perror("Erreur send connexion");exit(1);
    }
  }

  return res;
}

/**
 * @brief Transforme une chaîne de caractère pour enlever les majuscules et le retour à la ligne
 * 
 * @param m 
 */
void transformCommand(char m[]) {
  m[strcspn(m, "\n")] = 0;
  for(int i = 1; m[i]; i++){
    m[i] = tolower(m[i]);
  }
}

/**
 * @brief Fonction des threads clients, elle gère la réception d'un message envoyé par le client au serveur,
 *        et envoie ce message aux autres clients
 * @param parametres 
 * @return void* 
 */
void* client(void * parametres) {
  struct clientStruct* p = (struct clientStruct*) parametres;

  pthread_mutex_lock(&mutex);
  int dSC = p->dSC;
  pthread_mutex_unlock(&mutex);

  // Si l'utilisateur a réussi à se connecter
  if(connexion(dSC, p)) {
    int continu = 1;
    // Messages de l'utilisateur, tant qu'il n'indique pas fin/@d/@disconnect
    do {
      char msg[SIZE_MESSAGE];
      char t[SIZE_MESSAGE+60]; // A ENLEVER

      int r = recv(dSC, msg, SIZE_MESSAGE*sizeof(char), 0);
      if(-1 == r) {
        perror("Erreur recv client");exit(1);
      }
      strcpy(t, msg); //A ENLEVER SI BUG REGLE
      
      // Message normal, on envoie aux autres clients
      if(msg[0] != '@') {
        pthread_mutex_lock(&mutex);
        for(int i = 0; i<MAX_CLIENTS; i++) {
          if(clients[i] != NULL) {
            char str[3]; // CES 3 LIGNES AUSSI
            snprintf( str, 3, "%d", clients[i]->dSC);
            strcat(t, str);
            if(p->dSC != clients[i]->dSC && clients[i]->pseudo != NULL) {
              int s = send(clients[i]->dSC, t, strlen(t)+1, 0);
              if(-1 == s) {
                perror("Erreur send client");exit(1);
              }
            }
          }
        }
        pthread_mutex_unlock(&mutex);
      }
      else {
        transformCommand(msg);

        if(strcmp(msg, "@h") == 0 || strcmp(msg, "@help") == 0) {
          pthread_mutex_lock(&mutex_help);
          FILE *fileSource;
          fileSource = fopen("help.txt", "r");
          char ch;
          char help[SIZE_HELP] = "";
          while( ( ch = fgetc(fileSource) ) != EOF )
            strncat(help,&ch,1);
          fclose(fileSource);
         if(-1 == send(dSC, help, strlen(help)+1, 0)) {
            perror("Erreur send client");exit(1);
          }
          pthread_mutex_unlock(&mutex_help);
        }
        else if(strcmp(msg, "@all") == 0 || strcmp(msg, "@a") == 0) {
          char all[SIZE_MESSAGE] = "";
          pthread_mutex_lock(&mutex);
          for(int i=0; i<MAX_CLIENTS; i++) {
            if(clients[i] != NULL) {
              if(clients[i]->pseudo != NULL) {
                strcat(all, clients[i]->pseudo);
              }
            }
          }
          pthread_mutex_unlock(&mutex);
          if(-1 == send(dSC, all, strlen(all)+1, 0)) {
            perror("Erreur send client");exit(1);
          }
        }
        else if(strcmp(msg, "@d") == 0 || strcmp(msg, "@disconnect") == 0) {
          char all[SIZE_MESSAGE] = "Ta mère";
          if(-1 == send(dSC, all, strlen(all)+1, 0)) {
            perror("Erreur send client");exit(1);
          }
          continu = 0;
        }
      }
    } while(continu == 1);
  }

  // On indique dans le tableau que le client n'est plus connecté
  pthread_mutex_lock(&mutex);
  clients[p->numero] = NULL;
  p->dSC = -1;
  free(p->pseudo);
  free(p);
  pthread_mutex_unlock(&mutex);
  if(sem_post(&sem_place) == -1){
    perror("Erreur post sémaphore");
    exit(1);
  }

  pthread_exit(0);
}

/**
 * @brief Trouve la première place libre dans le tableau de client, -1 si une place n'a pas été trouvée
 * 
 * @param tab 
 * @param taille 
 * @return int 
 */
int getEmptyPosition(struct clientStruct * tab[], int taille) {
  int p = -1;
  for(int i=0; i<taille; i++) {
    if(tab[i] == NULL) {
      p = i;
      break;
    }
  }
  return p;
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

  //On initialise le sémaphore indiquant le nombre de place restante
  if(sem_init(&sem_place, 0, MAX_CLIENTS) == 1){
    perror("Erreur init sémaphore");
    exit(1);
  }

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

  // Liste qui contiendra les informations des clients
  clients = (struct clientStruct**)malloc(MAX_CLIENTS*sizeof(struct clientStruct *));
  for(int i=0; i<MAX_CLIENTS; i++) {
    clients[i] = NULL;
  }
  thread = (pthread_t*)malloc(MAX_CLIENTS*sizeof(pthread_t));

  signal(SIGINT, arret);

  nb_thread = 0;
  // On attend qu'un nouveau client veuille se connecter
  while(1) {
    // Tant que le nombre max de clients n'est pas atteint, on va attendre une connexion
    if(sem_wait(&sem_place) == 1){
      perror("Erreur wait sémaphore");
      exit(1);
    }
    struct sockaddr_in aC ;
    socklen_t lg = sizeof(struct sockaddr_in);
    int dSC = accept(dS, (struct sockaddr*)&aC,&lg) ;
    if(dSC == -1) {
      perror("Erreur accept");exit(1);
    }

    // On lance un thread pour chaque client, avec sa socket, son numéro de client, et la liste des clients
    puts("Client Ajouté");
    pthread_mutex_lock(&mutex);

    struct clientStruct * self = (struct clientStruct*) malloc(sizeof(struct clientStruct));
    self->dSC = dSC;
    self->pseudo = NULL; // Pas encore connecté
    self->numero = getEmptyPosition(clients, MAX_CLIENTS);

    clients[self->numero] = self;

    pthread_create(&thread[nb_thread++], NULL, client, (void*)self);
    for(int i=0; i<MAX_CLIENTS; i++) {
      if(clients[i] != NULL) {
        printf("%d : %d\n",i, clients[i]->dSC);
      }
    }
    //
    pthread_mutex_unlock(&mutex);
  }

  stopServeur(dS);
  exit(EXIT_SUCCESS);
}