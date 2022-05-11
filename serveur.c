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
#include <dirent.h>

#include "stack.h"

#define MAX_CLIENTS 5
#define MAX_FILES 3
#define SIZE_MESSAGE 256

int dS;
int dSFile;
pthread_mutex_t mutex_file; //Mutex pour l'accès à l'ensemble des fichiers du serveur
pthread_mutex_t mutex_clients; //Mutex pour la liste des Clients
pthread_mutex_t mutex_help; //Mutex pour le fichier help.txt
pthread_mutex_t mutex_thread; //Mutex pour la gestion des threads clients

//---START CLIENTS---------------------------------------------------------//
pthread_t file_manager;         //Thread gérant les demandes liés au transfert de fichiers
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
//---END CLIENTS-----------------------------------------------------------//


//---START FILES-----------------------------------------------------------//
pthread_t *thread_files;        //Liste des threads files
struct fileStruct ** files;     //Liste des files
sem_t sem_place_files;          //Sémaphore indiquant le nombre de place restante

pthread_t thread_cleaner_files; //Thread cleaner des threads files zombie
sem_t sem_thread_files;         //Sémaphore indiquant le nombre de thread file zombie à nettoyer
Stack * zombieStackFiles;       //Pile d'entier contenant les index des threads files zombies

// Idem pour l'upload d'un fichier
struct fileStruct {
  int dSF;
  char * filename;
  int size;
  int numero;
};
//---END FILES-------------------------------------------------------------//

/**
 * @brief Envoie un message au socket indiqué, et affiche l'erreur passé en paramètre s'il y a une erreur
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
 * @brief Essaye de récupérer un fichier, affiche une erreur s'il n'y arrive pas 
 * 
 * @param dS 
 * @param msg 
 * @param erreur 
 * @return int 
 */
int recvMessage(int dS, char msg[], char erreur[]) {
  int r = 0;
  if((r = recv(dS, msg, sizeof(char)*SIZE_MESSAGE, 0)) == -1) {
    perror(erreur);exit(1);
  }
  return r;
}

/**
 * @brief Ferme le serveur
 * 
 * @param dS 
 */
void stopServeur(int dS) {
  // Ferme le thread cleaner
  pthread_cancel(thread_cleaner);
  // Ferme les threads des clients
  pthread_mutex_lock(&mutex_clients);
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
  for(int i=0; i<MAX_FILES; i++) {
    if(files[i] != NULL) {
      free(files[i]);
    }
  }
  free(files);

  for (int i=0;i<MAX_CLIENTS;i++){
    pthread_cancel(thread[i]);
  }
  free(thread);
  for (int i=0;i<MAX_FILES;i++){
    pthread_cancel(thread_files[i]);
  }
  free(thread_files);
  pthread_mutex_unlock(&mutex_clients);
  pthread_mutex_destroy(&mutex_file);
  pthread_mutex_destroy(&mutex_clients);
  pthread_mutex_destroy(&mutex_help);
  pthread_mutex_destroy(&mutex_thread);

  //Clear les piles
  clearStack(zombieStack);
  free(zombieStack);
  clearStack(zombieStackFiles);
  free(zombieStackFiles);

  pthread_cancel(file_manager);

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
  pthread_mutex_lock(&mutex_clients);
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
  pthread_mutex_unlock(&mutex_clients);
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
    pthread_mutex_lock(&mutex_clients);
    p->pseudo = (char*)malloc(sizeof(char)*strlen(msg)+1);
    strcpy(p->pseudo, msg);
    pthread_mutex_unlock(&mutex_clients);
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
  pthread_mutex_lock(&mutex_clients);
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
  pthread_mutex_unlock(&mutex_clients);
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
 * @brief Envoie la liste des commandes au client en paramètre (lecture du fichier help.txt)
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
  pthread_mutex_lock(&mutex_clients);
  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i] != NULL) {
      if(clients[i]->pseudo != NULL) {
        strcat(all, clients[i]->pseudo);
        strcat(all, "\n");
      }
    }
  }
  pthread_mutex_unlock(&mutex_clients);
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
  pthread_mutex_lock(&mutex_clients);
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

  pthread_mutex_unlock(&mutex_clients);

  free(pseudoMessage);
  free(pseudo);
  free(message);
}

/**
 * @brief Liste les fichiers présents sur le serveur
 * 
 * @param dSC 
 */
void filesServeur(int dSC){
  int nb_file = 0; //variable pour compter le nombre de fichiers présents dans le dossier
  DIR *dir;
  struct dirent *ent;
  //On essaie d'ouvrir le dossier
  if ((dir = opendir("./download_server")) != NULL) {
    //On compte le nombre de fichiers présents dans le dossier
    while ((ent = readdir(dir)) != NULL) {
      if(strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 ){
        nb_file++;
      }
    }
    //Si le dossier n'est pas vide
    if(nb_file > 0){
      char *tab_file[nb_file]; //Variable qui va contenir le nom des fichiers
      nb_file = 0;
      char msg[SIZE_MESSAGE] = "Listes des fichiers présents dans le serveur : \n"; //Variable à destination du client qui va stocker les noms des fichiers 
      rewinddir(dir); //réinitialise le parcours du dossier
      while ((ent = readdir(dir)) != NULL) {
        //Pour chaque fichier du dossier, on récupère son nom et on le concatène dans msg
        if(strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 ){
          tab_file[nb_file] = malloc(sizeof(char) * (strlen(ent->d_name) + 1));
          strcpy(tab_file[nb_file],ent->d_name);
          strcat(msg, tab_file[nb_file]);
          strcat(msg, "\n");
          nb_file++;
        }
      }
      //Quand on a récupéré tous les fichiers, on envoie la liste au client
      sendMessage(dSC, msg, "Erreur envoi liste fichiers serveur");
      closedir(dir);
    }else{
      puts("Aucun fichier dans le dossier \"download_server\"");
    }
  }else{
    perror("Erreur open download_server");
    exit(1);
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

  pthread_mutex_lock(&mutex_clients);
  int dSC = p->dSC;
  pthread_mutex_unlock(&mutex_clients);

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
      // Commandes
      else {
        transformCommand(msg);
        //Help
        if(strcmp(msg, "@h") == 0 || strcmp(msg, "@help") == 0) {
          help(dSC);
        }
        //Liste des utilisateurs
        else if(strcmp(msg, "@all") == 0 || strcmp(msg, "@a") == 0) {
          listClients(dSC);
        }
        //Déconnexion
        else if(strcmp(msg, "@d") == 0 || strcmp(msg, "@disconnect") == 0) {
          continu = 0;
        }
        //Message privé
        else if(((msg[1] == 'm' && msg[2] == 'p') || (msg[1] == 'd' && msg[2] == 'm')) && isblank(msg[3])>0){
          dm(p, msg);
        }
        //Liste des fichiers disponibles dans le serveur
        else if(strcmp(msg, "@serveurfiles") == 0){
          filesServeur(dSC);
        }
        else {
          char erreur[SIZE_MESSAGE] = "Cette commande n'existe pas";
          sendMessage(dSC, erreur, "Erreur bad command");
        }
      }
    } while(continu == 1);
  }

  // On indique dans le tableau que le client n'est plus connecté
  pthread_mutex_lock(&mutex_clients);
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
  pthread_mutex_unlock(&mutex_clients);
  // Nombre de place disponible incrémenté
  if(sem_post(&sem_place) == -1){
    perror("Erreur post sémaphore nb_place_dispo");
    exit(1);
  }
  pthread_exit(0);
}

/**
 * @brief Fonction qui gère le protocole de copie d'un fichier client dans le serveur
 * 
 * @param f 
 */
void fileSend(struct fileStruct * f){
  char msg[SIZE_MESSAGE];
  
  recvMessage(f->dSF, msg, "Erreur recv filename");
  char path[SIZE_MESSAGE] = "./download_server/";
  strcat(path, msg);
  //Vérifier que le fichier existe pas déjà
  sendMessage(f->dSF, "OK", "Erreur confirm filename");
  if(1) {
    int size = 0;
    recvMessage(f->dSF, msg, "Erreur recv size");
    size = atoi(msg);
    sendMessage(f->dSF, "OK", "Erreur confirm size");

    char buffer[size];
    strcpy(buffer, "");

    int dataTotal = 0;
    int sizeToGet = size;
    do {
      sizeToGet = size - dataTotal > SIZE_MESSAGE ? SIZE_MESSAGE : size - dataTotal;
      if(sizeToGet > 0) {
        char data[sizeToGet+1];
        int dataGet = recv(f->dSF, data, sizeof(char)*sizeToGet, 0);
        if(dataGet == -1) {
          perror("Erreur recv file data");exit(1);
        }
        data[sizeToGet] = '\0';
        dataTotal += dataGet;
        strcat(buffer, data);
        sendMessage(f->dSF, "OK", "Erreur file confirm data");
      }
    } while(sizeToGet > 0);

    recvMessage(f->dSF, msg, "Erreur recv file END");
    if(strcmp(msg, "END") == 0) {
      // Enregistrer le fichier :
      FILE *fp = fopen(path, "wb"); // must use binary mode
      fwrite(buffer, sizeof(buffer[0]), size, fp); // writes an array of doubles
      fclose(fp);
    }
  }
}

/**
 * @brief Fonction qui gère le protocole d'envoie d'un fichier serveur au client
 * 
 * @param f 
 */
void fileReceive(struct fileStruct * f){

}

void* file(void * parametres) {
  struct fileStruct* f = (struct fileStruct *) parametres;

  char msg[SIZE_MESSAGE];
  //On attend de savoir quelle action on doit faire : RCV ou SEND
  recvMessage(f->dSF, msg, "Erreur recv protocol");
  //On envoie qu'on a bien reçu l'action
  sendMessage(f->dSF, "OK", "Erreur send ok for protocol");

  if(strcmp(msg, "SEND") == 0){
    fileSend(f);
  }
  else if(strcmp(msg,"RCV")==0){
    fileReceive(f);
  }

  pushStack(zombieStackFiles, f->numero);
  free(f->filename);
  free(f);
  // Nombre de place disponible incrémenté
  if(sem_post(&sem_place) == -1){
    perror("Erreur post sémaphore nb_place_dispo");
    exit(1);
  }
  pthread_exit(0);
}

/**
 * @brief Trouve la première place libre dans le tableau, -1 si une place n'a pas été trouvée
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
int getEmptyPositionFile(struct fileStruct * tab[], int taille) {
  int p = -1;
  for(int i=0; i<taille; i++) {
    if(tab[i] == NULL) {
      p = i;
      break;
    }
  }
  return p;
}

void* cleanerFiles() {
  while(1) {
    // On attends qu'un thread file se ferme
    if(sem_wait(&sem_thread_files) == 1) {
      perror("Erreur wait sémaphore file");exit(1);
    }

    int place = popStack(zombieStackFiles);
    void *valrep;
    int rep = pthread_join(thread_files[place], &valrep);
    if (valrep == PTHREAD_CANCELED)
      printf("The thread file was canceled - ");
    switch (rep) {
      case 0:
        printf("Thread File %d a été joinned \n", place);
        break;
      default:
        printf("Erreur durant le join du thread File : %d\n",place);
    }
  }
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
      perror("Erreur wait sémaphore client");exit(1);
    }

    pthread_mutex_lock(&mutex_clients);
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
    pthread_mutex_unlock(&mutex_clients);
  }
}

void ajoutFile(int dSF) {
  struct fileStruct * self = (struct fileStruct*) malloc(sizeof(struct fileStruct));
  self->dSF = dSF;
  self->filename = NULL; // Pas encore reçu les infos
  self->size = 0;
  //Faire un mutex
  self->numero = getEmptyPositionFile(files, MAX_FILES);

  files[self->numero] = self;
  char connexion[20] = "OK";
  sendMessage(dSF, connexion, "Erreur send connexion File");

  pthread_create(&thread_files[self->numero], NULL, file, (void*)self);
  puts("Demande File Ajouté");
}

/**
 * @brief Ajoute un client dans la liste, et lance le thread client associé
 * 
 * @param dSC 
 */
void ajoutClient(int dSC) {
  pthread_mutex_lock(&mutex_clients);

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

  pthread_mutex_unlock(&mutex_clients);
  puts("Client Ajouté");
}

void* acceptFiles() {
  // On attend une nouvelle demande de téléchargement de fichiers
  while(1) {
    // Tant que le nombre max de files n'est pas atteint, on va attendre une connexion
    if(sem_wait(&sem_place_files) == 1) {
      perror("Erreur wait sémaphore files");exit(1);
    }
    struct sockaddr_in aF;
    socklen_t lg = sizeof(struct sockaddr_in);
    int dSF = accept(dSFile, (struct sockaddr*)&aF,&lg) ;
    if(dSF == -1) {
      perror("Erreur accept File");exit(1);
    }

    // On lance un thread pour la demande d'upload de fichier
    ajoutFile(dSF);
  }
  pthread_exit(0);
}

void* acceptClients() {
  pthread_exit(0);
}

void initFiles(int port_file) {
  //On initialise le sémaphore indiquant le nombre de place restante
  if(sem_init(&sem_place_files, 0, MAX_FILES) == 1){
    perror("Erreur init sémaphore");exit(1);
  }

  dSFile = socket(PF_INET, SOCK_STREAM, 0);
  if(dSFile == -1) {
    perror("Erreur socket file");exit(1);
  }
  puts("Socket File Créé");

  struct sockaddr_in ad;
  ad.sin_family = AF_INET;
  ad.sin_addr.s_addr = INADDR_ANY;
  ad.sin_port = htons(port_file);
  if(-1 == bind(dSFile, (struct sockaddr*)&ad, sizeof(ad))) {
    perror("Erreur bind file");exit(1);
  }
  puts("Socket File Nommé");

  if(-1 == listen(dSFile, 7)) {
    perror("Erreur listen file");exit(1);
  }
  puts("Mode écoute File");

  // Liste qui contiendra les informations des clients
  files = (struct fileStruct**)malloc(MAX_FILES*sizeof(struct fileStruct *));
  for(int i=0; i<MAX_FILES; i++) {
    files[i] = NULL;
  }
  thread_files = (pthread_t*)malloc(MAX_FILES*sizeof(pthread_t));

  // Thread qui s'occupera des threads files zombies
  zombieStackFiles = createStack();
  pthread_create(&thread_cleaner_files, NULL, cleanerFiles, NULL);
}

void initClients(int port) {
  //On initialise le sémaphore indiquant le nombre de place restante
  if(sem_init(&sem_place, 0, MAX_CLIENTS) == 1){
    perror("Erreur init sémaphore");exit(1);
  }

  // Lancement du serveur
  dS = socket(PF_INET, SOCK_STREAM, 0);
  if(dS == -1) {
    perror("Erreur socket client");exit(1);
  }
  puts("Socket Client Créé");

  struct sockaddr_in ad;
  ad.sin_family = AF_INET;
  ad.sin_addr.s_addr = INADDR_ANY;
  ad.sin_port = htons(port);
  if(-1 == bind(dS, (struct sockaddr*)&ad, sizeof(ad))) {
    perror("Erreur bind client");exit(1);
  }
  puts("Socket Client Nommé");

  if(-1 == listen(dS, 7)) {
    perror("Erreur listen client");exit(1);
  }
  puts("Mode écoute Client");

  // Liste qui contiendra les informations des clients
  clients = (struct clientStruct**)malloc(MAX_CLIENTS*sizeof(struct clientStruct *));
  for(int i=0; i<MAX_CLIENTS; i++) {
    clients[i] = NULL;
  }
  thread = (pthread_t*)malloc(MAX_CLIENTS*sizeof(pthread_t));

  // Thread qui s'occupera des threads clients zombies
  zombieStack = createStack();
  pthread_create(&thread_cleaner, NULL, cleaner, NULL);
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
  const int port_file = port + 100;
  initFiles(port_file);
  initClients(port);

  pthread_create(&file_manager, NULL, acceptFiles, NULL);

  // CTRL-C
  signal(SIGINT, arret);

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