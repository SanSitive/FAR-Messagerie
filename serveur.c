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
#include <ctype.h>

#include "stack.h"

#define MAX_CLIENTS 5
#define SIZE_MESSAGE 256

int dS;
pthread_mutex_t mutex;
pthread_mutex_t mutex_help; //Mutex pour le fichier help.txt
pthread_mutex_t mutex_thread; //Mutex pour la gestion des threads clients

pthread_t *thread;              //Liste des threads
struct clientStruct ** clients; //Liste des clients
sem_t sem_place;                //Sémaphore indiquant le nombre de place restante

pthread_t thread_cleaner; //Thread cleaner des threads client-serveur zombie
sem_t sem_thread;         //Sémaphore indiquant le nombre de thread zombie à nettoyer
Stack * zombieStack;      //Pile d'entier contenant les index des threads zombies


// Structure décrivant un client, envoyé dans les threads
struct clientStruct {
  int dSC; //Socket
  char * pseudo;
  int numero; //Index dans le tableau de clients
};

/**
 * @brief Envoie un message au socket indiqué, et affiche l'erreur passé
 * 
 * @param dS 
 * @param msg 
 * @param erreur 
 */
void sendMessage(int dS, char msg[], char erreur[]) {
  if(-1 == send(dS, msg, strlen(msg)+1, 0)) {
    perror(erreur);exit(1);
  }
}

/**
 * @brief Ferme le socket serveur
 * 
 * @param dS 
 */
void stopServeur(int dS) {
  // Ferme le thread cleaner
  pthread_cancel(thread_cleaner);
  // Ferme les threads des clients
  pthread_mutex_lock(&mutex);
  char msg[20] = "@shutdown";
  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i] != NULL) {
      sendMessage(clients[i]->dSC, msg, "Erreur send shutdown");
      if(clients[i]->pseudo != NULL)
        free(clients[i]->pseudo);
      free(clients[i]);
    }
  }
  free(clients);

  for (int i=0;i<MAX_CLIENTS;i++){
    pthread_cancel(thread[i]);
  }
  free(thread);
  pthread_mutex_unlock(&mutex);
  pthread_mutex_destroy(&mutex);

  //Clear la pile
  clearStack(zombieStack);
  free(zombieStack);


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
 * @brief Vérifie les informations de login d'un client.
 *        Retourne 1 si les informations sont bonnes, 0 sinon, ou si le pseudo est déjà pris
 * @param dSC
 * @param p
 * @return int
 */
int login(int dSC, struct clientStruct * p) {
  int res = 1;
  char msg[SIZE_MESSAGE];
  // Récupérer les infos
  if(-1 == recv(p->dSC, msg, SIZE_MESSAGE*sizeof(char), 0)) {
    perror("Erreur recv login");exit(1);
  }

  // Vérification
  if(isPseudoTaken(msg) == 1) res = 0;

  // Réponse au client
  if(res == 1) {
    char retour[20] = "OK";
    sendMessage(dSC, retour, "Erreur send login");
    pthread_mutex_lock(&mutex);
    p->pseudo = (char*)malloc(sizeof(char)*strlen(msg)+1);
    strcpy(p->pseudo, msg);
    pthread_mutex_unlock(&mutex);
  }
  else {
    char retour[20] = "PseudoTaken";
    sendMessage(dSC, retour, "Erreur send PseudoTaken");
  }

  return res;
}

/**
 * @brief Envoie le message d'un client vers tous les autres
 * 
 * @param p 
 * @param msg 
 */
void clientToAll(struct clientStruct* p, char msg[]) {
  pthread_mutex_lock(&mutex);
  char msgPseudo[SIZE_MESSAGE];
  strcpy(msgPseudo, p->pseudo);
  strcat(msgPseudo, " : ");
  strcat(msgPseudo, msg);
  for(int i = 0; i<MAX_CLIENTS; i++) {
    if(clients[i] != NULL) {
      if(p->dSC != clients[i]->dSC && clients[i]->pseudo != NULL) {
        sendMessage(clients[i]->dSC, msgPseudo, "Erreur send clientToAll");
      }
    }
  }
  pthread_mutex_unlock(&mutex);
}

/**
 * @brief Transforme une chaîne de caractère pour enlever les majuscules(pour ce qui suit directement @, pas les paramètres), 
 *        les espaces en trop à la fin, et le retour à la ligne
 * 
 * @param m 
 */
void transformCommand(char m[]) {
  // Enlever \n
  m[strcspn(m, "\n")] = 0;

  // Enlever les espaces à la fin
  for(int i=strlen(m); i>0; i--) {
    if(isblank(m[i])>0) {
      m[i] = 0;
    }
    else {
      break;
    }
  }

  // Enlever les majuscules de ce qui suit directement @
  for(int i = 1; m[i]; i++){
    if(isblank(m[i])>0) {
      break;
    }
    m[i] = tolower(m[i]);
  }
}

/**
 * @brief Envoie de la liste des commandes au client en paramètre (lu du fichier help.txt)
 * 
 * @param dSC 
 */
void help(int dSC) {
  pthread_mutex_lock(&mutex_help);
  FILE *fileSource;
  fileSource = fopen("help.txt", "r");
  char ch;
  char help[SIZE_MESSAGE] = "";
  while( ( ch = fgetc(fileSource) ) != EOF )
    strncat(help,&ch,1);
  fclose(fileSource);
  sendMessage(dSC, help, "Erreur send help");
  pthread_mutex_unlock(&mutex_help);
}

/**
 * @brief Envoie la liste des utilisateurs (sauf lui-même) au client en paramètre
 * 
 * @param dSC 
 */
void listClients(int dSC) {
  char all[SIZE_MESSAGE] = "";
  pthread_mutex_lock(&mutex);
  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i] != NULL) {
      if(clients[i]->pseudo != NULL) {
        strcat(all, clients[i]->pseudo);
        strcat(all, "\n");
      }
    }
  }
  pthread_mutex_unlock(&mutex);
  sendMessage(dSC, all, "Erreur send listClients");
}

/**
 * @brief Envoie un message privé à un client
 *        Si le destinataire n'existe pas, on envoie un message d'erreur à l'émetteur
 * 
 * @param msg 
 */
void dm(struct clientStruct* p, char msg[]) {
  //On créer une place pour le message et le pseudo
  int taillePM = strlen(msg) - 3;
  char *pseudoMessage = (char*)malloc(taillePM);
  //On récupère le pseudo et le message dans un premier temps
  strncpy(pseudoMessage, msg + 3, taillePM);
  //On cherche où est l'espace
  int j = 0;
  int debutP = 0;
  while(debutP == 0){
    if (!(isblank(pseudoMessage[j])>0)){
      debutP = j;
    }
    j++;
  }
  int finP = 0 ;
  while(finP == 0){
    if (isblank(pseudoMessage[j])>0){
      finP = j - 1;
    } 
    j++;
  }

  int debutM = 0;
  while((debutM == 0) && j < taillePM){
    if (!(isblank(pseudoMessage[j])>0)){
      debutM = j;
    }
    j++;
  }
  
  //On créer une place pour le pseudo
  int tailleP = finP - debutP + 1;
  char *pseudo = (char*)malloc(tailleP + 1);
  strncpy(pseudo, pseudoMessage + debutP, tailleP);
  pseudo[tailleP] = '\0';
  //On créer une place pour le message
  int tailleM = strlen(pseudoMessage) - debutM + 1;
  char *message = (char*)malloc(tailleM);
  strncpy(message, pseudoMessage + debutM, tailleM);

  //On cherche le pseudo dans la liste des pseudos existants 
  pthread_mutex_lock(&mutex);
  int found = 0;

  //Si le client s'envoie un message à lui même
  if(strcmp(p->pseudo, pseudo) == 0){
          found = 2;
  }else{
    for(int i=0; i<MAX_CLIENTS; i++) {
      if(clients[i] != NULL) { //On vérifie que le client existe
        if(p->dSC != clients[i]->dSC && clients[i]->pseudo != NULL) { //On vérifie que le client est connecté
          if(strcmp(pseudo, clients[i]->pseudo) == 0){
            char msgComplet[SIZE_MESSAGE];
            strcpy(msgComplet, p->pseudo);
            strcat(msgComplet, " (mp) : ");
            strcat(msgComplet, message);
            sendMessage(clients[i]->dSC, msgComplet, "Erreur send dm");
            found = 1;
            break;
          }
        }
      }
    }
  }
  //Si le pseudo n'appartient à personne
  if(found == 0) {
    char erreur[SIZE_MESSAGE] = "Cet utilisateur n'existe pas ou n'est pas connecté";
    sendMessage(p->dSC, erreur, "Erreur send erreur dm found == 0");
  }else if(found == 2){
    char erreur[SIZE_MESSAGE] = "Vous ne pouvez pas vous envoyer un message à vous même";
    sendMessage(p->dSC, erreur, "Erreur send erreur dm found == 2");
  }

  pthread_mutex_unlock(&mutex);

  free(pseudoMessage);
  free(pseudo);
  free(message);
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
  if(login(dSC, p)) {
    int continu = 1;
    // Messages de l'utilisateur, tant qu'il n'indique pas @d/@disconnect
    do {
      char msg[SIZE_MESSAGE];
      int r = recv(dSC, msg, SIZE_MESSAGE*sizeof(char), 0);
      if(-1 == r) {
        perror("Erreur recv client");exit(1);
      }
      
      // Message normal, on envoie aux autres clients
      if(msg[0] != '@') {
        clientToAll(p, msg);
      }
      // Commande
      else {
        transformCommand(msg);

        if(strcmp(msg, "@h") == 0 || strcmp(msg, "@help") == 0) {
          help(dSC);
        }
        else if(strcmp(msg, "@all") == 0 || strcmp(msg, "@a") == 0) {
          listClients(dSC);
        }
        else if(strcmp(msg, "@d") == 0 || strcmp(msg, "@disconnect") == 0) {
          continu = 0;
        }
        else if(((msg[1] == 'm' && msg[2] == 'p') || (msg[1] == 'd' && msg[2] == 'm')) && isblank(msg[3])>0){
          dm(p, msg);
        }
        else {
          char erreur[SIZE_MESSAGE] = "Cette commande n'existe pas";
          sendMessage(dSC, erreur, "Erreur bad command");
        }
      }
    } while(continu == 1);
  }

  // On indique dans le tableau que le client n'est plus connecté
  pthread_mutex_lock(&mutex);
  pthread_mutex_lock(&mutex_thread);
  // On indique qu'il faut s'occuper du thread zombie
  if(sem_post(&sem_thread) == -1){
    perror("Erreur post sémaphore nb_thread_zombie");
    exit(1);
  }
  pushStack(zombieStack, p->numero);
  pthread_mutex_unlock(&mutex_thread);
  clients[p->numero] = NULL;
  p->dSC = -1;
  free(p->pseudo);
  free(p);
  pthread_mutex_unlock(&mutex);
  // Nombre de place disponible incrémenté
  if(sem_post(&sem_place) == -1){
    perror("Erreur post sémaphore nb_place_dispo");
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
 * @brief Fonction lié au thread qui clean en boucle les thread zombie s'il en existe
 * 
 * @return void*
 */
void* cleaner(){
  while(1){
    // On attends qu'un thread client se ferme
    if(sem_wait(&sem_thread) == 1){
      perror("Erreur wait sémaphore");
      exit(1);
    }

    pthread_mutex_lock(&mutex);
    pthread_mutex_lock(&mutex_thread);
    int place = popStack(zombieStack);
    void *valrep;
    int rep = pthread_join(thread[place], &valrep);
    pthread_mutex_unlock(&mutex_thread);
    if (valrep == PTHREAD_CANCELED)
      printf("The thread was canceled - ");
    switch (rep) {
      case 0:
        printf("Thread %d a été joinned \n", place);
        break;
      default:
        printf("Erreur durant le join du thread : %d\n",place);
    }
    pthread_mutex_unlock(&mutex);
  }
}

/**
 * @brief Ajoute un client dans la liste, et lance le thread client associé
 * 
 * @param dSC 
 */
void ajoutClient(int dSC) {
  pthread_mutex_lock(&mutex);

  struct clientStruct * self = (struct clientStruct*) malloc(sizeof(struct clientStruct));
  self->dSC = dSC;
  self->pseudo = NULL; // Pas encore connecté
  self->numero = getEmptyPosition(clients, MAX_CLIENTS);

  clients[self->numero] = self;
  // Client connecté, on lui envoie la confirmation
  char connexion[20] = "OK";
  sendMessage(dSC, connexion, "Erreur send connexion");

  pthread_mutex_lock(&mutex_thread);
  pthread_create(&thread[self->numero], NULL, client, (void*)self);
  pthread_mutex_unlock(&mutex_thread);

  pthread_mutex_unlock(&mutex);
  puts("Client Ajouté");
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
    puts("Lancement : ./client port");exit(1);
  }

  const int port = atoi(argv[1]);

  //On initialise le sémaphore indiquant le nombre de place restante
  if(sem_init(&sem_place, 0, MAX_CLIENTS) == 1){
    perror("Erreur init sémaphore");exit(1);
  }

  // Lancement du serveur
  dS = socket(PF_INET, SOCK_STREAM, 0);
  if(dS == -1) {
    perror("Erreur socket");exit(1);
  }
  puts("Socket Créé");

  struct sockaddr_in ad;
  ad.sin_family = AF_INET;
  ad.sin_addr.s_addr = INADDR_ANY;
  ad.sin_port = htons(port);
  if(-1 == bind(dS, (struct sockaddr*)&ad, sizeof(ad))) {
    perror("Erreur bind");exit(1);
  }
  puts("Socket Nommé");

  if(-1 == listen(dS, 7)) {
    perror("Erreur listen");exit(1);
  }
  puts("Mode écoute");

  // Liste qui contiendra les informations des clients
  clients = (struct clientStruct**)malloc(MAX_CLIENTS*sizeof(struct clientStruct *));
  for(int i=0; i<MAX_CLIENTS; i++) {
    clients[i] = NULL;
  }
  thread = (pthread_t*)malloc(MAX_CLIENTS*sizeof(pthread_t));

  signal(SIGINT, arret);

  // Thread qui s'occupera des threads clients zombies
  zombieStack = createStack();
  pthread_create(&thread_cleaner, NULL, cleaner, NULL);

  // On attend qu'un nouveau client veuille se connecter
  while(1) {
    // Tant que le nombre max de clients n'est pas atteint, on va attendre une connexion
    if(sem_wait(&sem_place) == 1) {
      perror("Erreur wait sémaphore");exit(1);
    }
    struct sockaddr_in aC ;
    socklen_t lg = sizeof(struct sockaddr_in);
    int dSC = accept(dS, (struct sockaddr*)&aC,&lg) ;
    if(dSC == -1) {
      perror("Erreur accept");exit(1);
    }

    // On lance un thread pour chaque client, avec sa socket, son numéro de client, et la liste des clients
    ajoutClient(dSC);
  }

  stopServeur(dS);
  exit(EXIT_SUCCESS);
}