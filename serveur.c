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
#include <sys/stat.h>

#include "stack.h"

#define MAX_CLIENTS 5
#define MAX_FILES 3
#define SIZE_MESSAGE 256
#define MAX_CHANNEL 10
#define MAX_CLIENTS_CHANNEL 10

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
  struct channelStruct * channel;
};
//---END CLIENTS-----------------------------------------------------------//

//---START CHANNELS--------------------------------------------------------//
// Structure d'un salon, stocké dans le tableaux des clients (chaque clients appartient à un salon)
struct channelStruct ** channels; //Liste des channels
sem_t sem_place_channels;         //Sémaphore indiquant si le nombre de channel créable possible
pthread_mutex_t mutex_channel_file;
pthread_mutex_t mutex_channel_place;

struct channelStruct {
  int capacity; //Nombre max de client dans un channel 
  int count; //Nombre actuel de client dans le channel
  char * description;
  char * name; //Le nom du channel grace auquel on peut s'y connecter
  int numero; //Index dans le tableau des channels
};
//---END CHANNELS----------------------------------------------------------//


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
 * @return Nombre d'octets envoyé, ou -1 s'il y a une erreur
 */
int sendMessage(int dS, char msg[], char erreur[]) {
  int r = 0;
  if((r = send(dS, msg, SIZE_MESSAGE, 0)) == -1 ) {
    perror(erreur);exit(1);
  }
  return r;
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
 * @brief Créée une socket en mode écoute, à partir d'un port
 * 
 * @return int 
 */
int createSocketServer(int port) {
  int dS = socket(PF_INET, SOCK_STREAM, 0);
  if(dS == -1) {
    perror("Erreur socket");exit(1);
  }

  struct sockaddr_in ad;
  ad.sin_family = AF_INET;
  ad.sin_addr.s_addr = INADDR_ANY;
  ad.sin_port = htons(port);
  if(-1 == bind(dS, (struct sockaddr*)&ad, sizeof(ad))) {
    perror("Erreur bind");exit(1);
  }

  if(-1 == listen(dS, 7)) {
    perror("Erreur listen");exit(1);
  }
  return dS;
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
  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i] != NULL) {
      sendMessage(clients[i]->dSC, "@shutdown", "Erreur send shutdown");
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
  //A RAJOUTER : la save des channels dans le fichier channel.txt avant le free
  pthread_mutex_lock(&mutex_channel_place);
  for(int i=0; i<MAX_CHANNEL; i++) {
    if(channels[i] != NULL) {
      if(channels[i]->name != NULL && channels[i]->description != NULL){
        free(channels[i]->name);
      }
      if(channels[i]->description != NULL){
        free(channels[i]->description);
      }
      free(channels[i]);
    }
  }
  free(channels);
  pthread_mutex_unlock(&mutex_channel_place);

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
  pthread_mutex_destroy(&mutex_channel_place);
  pthread_mutex_destroy(&mutex_channel_file);
  sem_destroy(&sem_place);
  sem_destroy(&sem_thread);
  sem_destroy(&sem_place_channels);
  sem_destroy(&sem_place_files);
  sem_destroy(&sem_thread_files);


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
  recvMessage(p->dSC, msg, "Erreur recv login");
  
  //Vérification
  if(strcmp(msg, "@disconnect") == 0 || strlen(msg) <1) return 0;
  else if(isPseudoTaken(msg) == 1) res = 0;

  // Réponse au client
  if(res == 1) {
    sendMessage(dSC, "OK", "Erreur send login");
    pthread_mutex_lock(&mutex_clients);
    p->pseudo = (char*)malloc(sizeof(char)*strlen(msg)+1);
    strcpy(p->pseudo, msg);
    pthread_mutex_unlock(&mutex_clients);
  }
  else {
    sendMessage(dSC, "PseudoTaken", "Erreur send PseudoTaken");
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
  char msgPseudo[SIZE_MESSAGE];
  pthread_mutex_lock(&mutex_clients);
  strcpy(msgPseudo, p->pseudo);
  pthread_mutex_unlock(&mutex_clients);
  strcat(msgPseudo, " : ");
  int sizeMsgPseudo = strlen(msgPseudo);
  int sizeMsg = strlen(msg) + 1;

  int toSend = sizeMsg;
  int nbSend = 0;
  //Tant qu'on a pas envoyé le message en entier
  while(toSend > nbSend) {
    char msgToSend[SIZE_MESSAGE];
    strncpy(msgToSend, msgPseudo, sizeMsgPseudo);
    msgToSend[sizeMsgPseudo] = '\0';
    //Si ce qu'il reste à envoyer dépasse la taille disponible (taille totale disponible - la taille que prend le pseudo + ':')
    if(toSend - nbSend > SIZE_MESSAGE - sizeMsgPseudo) {
      int sizeToSend = (SIZE_MESSAGE - sizeMsgPseudo) - 1; // -1 pour pouvoir mettre '\0'
      strncat(msgToSend, msg+nbSend, sizeToSend);
      msgToSend[SIZE_MESSAGE-1] = '\0';
      nbSend += sizeToSend;
    }
    //Sinon on peut directement ajouter ce qu'il reste
    else {
      strcat(msgToSend, msg+nbSend);
      nbSend = toSend;
    }
    printf("\n%s\n", msgToSend);
    //On envoie le message aux autres clients
    pthread_mutex_lock(&mutex_clients);
    for(int i = 0; i<MAX_CLIENTS; i++) {
      if(clients[i] != NULL) {
        if(p->dSC != clients[i]->dSC && clients[i]->pseudo != NULL) {
          sendMessage(clients[i]->dSC, msgToSend, "Erreur send clientToAll");
        }
      }
    }
    pthread_mutex_unlock(&mutex_clients);
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

  //Gestion erreur fopen 
  if (fileSource == NULL){
    //On averti le client
    sendMessage(dSC, "Erreur de la commande help, veuillez réessayer.", "Erreur sending erreur with fopen");
  }else{ //pas d'erreur
    char ch;
    char help[SIZE_MESSAGE];
    //On vide le help (erreur nouvelle)
    help[0] = '\0';
    int count = 0;
    while( ( ch = fgetc(fileSource) ) != EOF ){
      strncat(help,&ch,1);
      count++;
      if(ch == '\n' || count == 255){
        
        //On remplace le dernier caractère par un \0
        if(ch == '\n'){
          help[count - 1] = '\0';
        }else{
          help[count] = '\0';
        }
        sendMessage(dSC, help, "");
        strcpy(help,"");
        count = 0;
      }
    }
    if(count > 0){
      help[count] = '\0';
      sendMessage(dSC, help, "");
    }
  }
  fclose(fileSource);
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
  int taillePM = strlen(msg) - 3 + 1;
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
  char *message = (char*)malloc(tailleM + 1);
  strncpy(message, pseudoMessage + debutM, tailleM);
  message[tailleM] = '\0';

  //On cherche le pseudo dans la liste des pseudos existants 
  pthread_mutex_lock(&mutex_clients);
  int error = 1;

  //Si le client s'envoie un message à lui même
  if(strcmp(p->pseudo, pseudo) == 0){
    error = 2;
  }
  //Si la taille du message en comptant le pseudo est trop grande
  else if(strlen(p->pseudo) + strlen(" (mp) : ") + strlen(message) > SIZE_MESSAGE) {
    error = 3;
  }
  else{
    for(int i=0; i<MAX_CLIENTS; i++) {
      if(clients[i] != NULL) { //On vérifie que le client existe
        if(p->dSC != clients[i]->dSC && clients[i]->pseudo != NULL) { //On vérifie que le client est connecté
          if(strcmp(pseudo, clients[i]->pseudo) == 0){
            char msgComplet[SIZE_MESSAGE];
            strcpy(msgComplet, p->pseudo);
            strcat(msgComplet, " (mp) : ");
            strcat(msgComplet, message);
            sendMessage(clients[i]->dSC, msgComplet, "Erreur send dm");
            error = 0;
            break;
          }
        }
      }
    }
  }
  //Si le pseudo n'appartient à personne
  if(error == 1) {
    sendMessage(p->dSC, "Cet utilisateur n'existe pas ou n'est pas connecté", "Erreur send erreur dm found == 0");
  }else if(error == 2){
    sendMessage(p->dSC, "Vous ne pouvez pas vous envoyer un message à vous même", "Erreur send erreur dm found == 2");
  }
  else if(error == 3) {
    sendMessage(p->dSC, "La taille de votre message est trop grande, réduisez la ou envoyez en plusieurs fois", "Erreur send erreur dm found == 2");
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

void allChannels(int dSC){
  pthread_mutex_lock(&mutex_channel_place);
  for(int i =0; i<MAX_CHANNEL; i++){
    if(channels[i] != NULL){
      sendMessage(dSC,channels[i]->name,"Erreur envoi channel");
    }
  }
  pthread_mutex_unlock(&mutex_channel_place);
}

/**
 * @brief Fonction des threads clients, elle gère la réception d'un message envoyé par le client au serveur,
 *        et envoie ce message aux autres clients
 * @param parametres Struct clientStruct
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
      recvMessage(dSC, msg, "Erreur recv client");
      
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
        else if(strcmp(msg,"@channels") == 0){
          allChannels(dSC);
        }
        else {
          sendMessage(dSC, "Cette commande n'existe pas", "Erreur bad command");
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
  //free(p->pseudo);
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
void fileToServer(struct fileStruct * f){
  char msg[SIZE_MESSAGE];
  
  recvMessage(f->dSF, msg, "Erreur recv filename");
  char path[SIZE_MESSAGE] = "./download_server/";
  strcat(path, msg);
  FILE *fp = fopen(path, "wb");

  if(fp == NULL) {
    sendMessage(f->dSF, "ERR", "Erreur ouverture filename");
  }
  else {
    sendMessage(f->dSF, "OK", "Erreur confirm filename");

    int size = 0;
    recvMessage(f->dSF, msg, "Erreur recv size");
    size = atoi(msg);
    sendMessage(f->dSF, "OK", "Erreur confirm size");

    recvMessage(f->dSF,msg,"Erreur recv open ok");
    //On a bien réussi à ouvrir le fichier
    if(strcmp(msg, "OK") == 0){
      int dataTotal = 0;
      int sizeToGet = size;
      do {
        sizeToGet = size - dataTotal > SIZE_MESSAGE ? SIZE_MESSAGE : size - dataTotal;
        if(sizeToGet > 0) {
          char *data = (char*)malloc(sizeof(char)*sizeToGet);
          int dataGet = recv(f->dSF, data, sizeof(char)*sizeToGet, 0);
          if(dataGet == -1) {
            perror("Erreur recv file data");exit(1);
          }
          dataTotal += dataGet;
          fwrite(data, sizeof(data[0]), sizeToGet, fp);
          sendMessage(f->dSF, "OK", "Erreur file confirm data");
          free(data);
        }
      } while(sizeToGet > 0);
    }
  }
  fclose(fp);
}

/**
 * @brief Fonction qui gère le protocole d'envoie d'un fichier serveur au client
 * 
 * @param f 
 */
void fileToClient(struct fileStruct * f){
  char m[SIZE_MESSAGE];
  //On attends le nom du fichier
  recvMessage(f->dSF, m, "Erreur filename");

  pthread_mutex_lock(&mutex_file);
  
  struct stat st;
  char path[SIZE_MESSAGE] = "./download_server/";
  strcat(path, m);
  
  if (stat(path, &st) == -1){
    sendMessage(f->dSF,"FileNotExists", "Erreur sending error size");
  }else{
    int size = st.st_size;
    char sizeString[10];
    sprintf(sizeString, "%d", size);

    pthread_mutex_unlock(&mutex_file);

    sendMessage(f->dSF,"OK", "Erreur confirm file name to client");
    
    //On attends le READY
    recvMessage(f->dSF, m, "Erreur receive READY");

    if(strcmp(m, "READY") == 0) {
      //On envoie la taille du fichier demandé
      sendMessage(f->dSF, sizeString, "Erreur send size");
      //On attend la confirmation du client
      recvMessage(f->dSF, m, "Erreur file protocol");

      if(strcmp(m, "OK") == 0){
        pthread_mutex_lock(&mutex_file);
        FILE * fp = fopen(path, "rb");
        
        //Gestion erreur fopen 
        if (fp == NULL){
          //On averti le client
          sendMessage(f->dSF, "ERR", "Erreur sending erreur with fopen");
        }else{
          sendMessage(f->dSF, "OK", "Erreur sending ok with fopen");

          //On attend la confirmation du client pour envoyer
          recvMessage(f->dSF, m, "Erreur file protocol");
          if(strcmp(m, "OK") == 0) {
            int dataSent = 0;
            while(dataSent < size && strcmp(m, "OK") == 0) {
              int sizeToGet = size - dataSent > SIZE_MESSAGE ? SIZE_MESSAGE : size - dataSent;
              char *data = (char*)malloc(sizeof(char)*sizeToGet);
              for(int i=0; i<sizeToGet; i++) {
                data[i] = 0;
              }
              dataSent += fread(data, sizeof(char), sizeToGet, fp);

              int sent = send(f->dSF, data, sizeof(char)*sizeToGet, 0);
              if(sent == -1) {
                perror("Erreur send data file");exit(1);
              }
              recvMessage(f->dSF, m, "Erreur recv file OK");
              free(data);
            }
          }
        }
        fclose(fp);
      }  
    }
  }
  pthread_mutex_unlock(&mutex_file);
}

/**
 * @brief Thread lié à une demande de téléchargement d'un fichier
 * 
 * @param parametres Struct fileStruct
 * @return void* 
 */
void* file(void * parametres) {
  struct fileStruct* f = (struct fileStruct *) parametres;

  char msg[SIZE_MESSAGE];
  //On attend de savoir quelle action on doit faire : RCV ou SEND
  recvMessage(f->dSF, msg, "Erreur recv protocol");
  //On envoie qu'on a bien reçu l'action
  sendMessage(f->dSF, "OK", "Erreur send ok for protocol");

  if(strcmp(msg, "SEND") == 0){
    fileToServer(f);
  }
  else if(strcmp(msg,"RCV")==0){
    fileToClient(f);
  }

  pushStack(zombieStackFiles, f->numero);
  files[f->numero] = NULL;
  free(f->filename);
  free(f);
  // Nombre de place disponible incrémenté
  if(sem_post(&sem_place) == -1){
    perror("Erreur post sémaphore nb_place_dispo");exit(1);
  }
  pthread_exit(0);
}

/**
 * @brief Trouve la première place libre dans la liste des clients, -1 si une place n'a pas été trouvée
 * 
 * @param tab 
 * @param taille 
 * @return index dans le tableau de la place disponible, -1 si aucune place n'est disponible
 */
int getEmptyPositionClient(struct clientStruct * tab[], int taille) {
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
 * @brief Trouve la première place libre dans la liste des fichiers, -1 si une place n'a pas été trouvée
 * 
 * @param tab 
 * @param taille 
 * @return index dans le tableau de la place disponible, -1 si aucune place n'est disponible
 */
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
/**
 * @brief Trouve la première place libre dans la liste des channels, -1 si une place n'a pas été trouvée
 * 
 * @param tab 
 * @param taille 
 * @return index dans le tableau de la place disponible, -1 si aucune place n'est disponible
 */
int getEmptyPositionChannels(struct channelStruct * tab[], int taille) {
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
 * @brief Fonction lié au thread qui clean en boucle les thread files zombies s'il en existe
 * 
 * @return void*
 */
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
 * @brief Fonction lié au thread qui clean en boucle les thread clients zombies s'il en existe
 * 
 * @return void*
 */
void* cleanerClients(){
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

/**
 * @brief Ajoute une socket liée au fichier dans la liste et lance le thread file associé
 * 
 * @param dSF 
 */
void addFileSocket(int dSF) {
  struct fileStruct * self = (struct fileStruct*) malloc(sizeof(struct fileStruct));
  self->dSF = dSF;
  self->filename = NULL; // Pas encore reçu les infos
  self->size = 0;
  //Faire un mutex
  self->numero = getEmptyPositionFile(files, MAX_FILES);

  files[self->numero] = self;
  sendMessage(dSF, "OK", "Erreur send connexion File");

  pthread_create(&thread_files[self->numero], NULL, file, (void*)self);
  puts("Demande File Ajouté");
}

/**
 * @brief Ajoute un client dans la liste, et lance le thread client associé
 * 
 * @param dSC 
 */
void addClientSocket(int dSC) {
  pthread_mutex_lock(&mutex_clients);

  struct clientStruct * self = (struct clientStruct*) malloc(sizeof(struct clientStruct));
  self->dSC = dSC;
  self->pseudo = NULL; // Pas encore connecté
  self->numero = getEmptyPositionClient(clients, MAX_CLIENTS);
  pthread_mutex_lock(&mutex_channel_place);
  self->channel = channels[0];
  pthread_mutex_unlock(&mutex_channel_place);
  printf("Le channel du client est %s\n",self->channel->name);

  clients[self->numero] = self;
  // Client connecté, on lui envoie la confirmation
  sendMessage(dSC, "OK", "Erreur send connexion");

  pthread_mutex_lock(&mutex_thread);
  pthread_create(&thread[self->numero], NULL, client, (void*)self);
  pthread_mutex_unlock(&mutex_thread);

  pthread_mutex_unlock(&mutex_clients);
  puts("Client Ajouté");
}

/**
 * @brief Boucle infinie attendant de nouvelles demandes liées aux fichiers
 * 
 * @return void* 
 */
void* acceptFiles() {
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
    addFileSocket(dSF);
  }
  pthread_exit(0);
}

/**
 * @brief Boucle infinie attendant de nouveaux clients
 * 
 */
void acceptClients() {
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
    addClientSocket(dSC);
  }
}

/**
 * @brief Initialise et récupère les channels sauvegardés
 * 
 */
void initChannels() {
  //On initialise le sémaphore indiquant le nombre de place restante
  if(sem_init(&sem_place_channels, 0, MAX_CHANNEL) == 1){
    perror("Erreur init sémaphore nb_place_channel");exit(1);
  }

  // Liste qui contiendra les informations des clients
  channels = (struct channelStruct**)malloc(MAX_CHANNEL*sizeof(struct channelStruct *));
  for(int i=0; i<MAX_CHANNEL; i++) {
    channels[i] = NULL;
  }

  pthread_mutex_lock(&mutex_channel_file);
  pthread_mutex_lock(&mutex_channel_place);
  FILE *fileSource;
  fileSource = fopen("channel.txt", "r");

  //Gestion erreur fopen 
  if (fileSource == NULL){
    //On averti le client ? On redemarre le serveur ? fonction dans main
    fclose(fileSource);
    pthread_mutex_unlock(&mutex_channel_file);
    pthread_mutex_unlock(&mutex_channel_place);
  }else{ //pas d'erreur
    char ch;
    char chan[SIZE_MESSAGE];
    int count = 0;
    int END = 0;
    while( (( ch = fgetc(fileSource) ) != EOF) &&  END == 0 ){
      strncat(chan,&ch,1);
      count++;

      if(strcmp(chan,"END") == 0){
        strcpy(chan,"");
        count = 0;
        END = 1;
      }else if(ch == '\n' || count == 255){
        
        //On remplace le dernier caractère par un \0
        if(ch == '\n'){
          chan[count-1] = '\0';
        }
        
        //Début instanciation du salon
        struct channelStruct * self = (struct channelStruct*) malloc(sizeof(struct channelStruct));
        self->capacity = MAX_CLIENTS_CHANNEL;
        self->count = 0;
        self->name = malloc((strlen(chan)+1)*sizeof(char));
        strcpy(self->name,chan);
        
        char *temp = "Description générique pour l'instant";
        self->description = malloc((strlen(temp)+1)*sizeof(char));
        strcpy(self->description,"Description générique pour l'instant");

        self->numero = getEmptyPositionChannels(channels,MAX_CHANNEL);
        
        channels[self->numero] = self;
        //Fin d'instanciation du salon
        strcpy(chan,"");
        count = 0;
      }
    }
    //On met la capacité du channel général = au max de clients
    for(int i=0; i<MAX_CHANNEL; i++){
      if(channels[i] != NULL){
        char temp[8];
        for(int j = 0; j<7; j++){
          temp[j] = channels[i]->name[j];
        }
        if(strcmp(temp,"General") == 0){
          strcpy(channels[i]->description,"Le channel général");
          channels[i]->capacity = MAX_CLIENTS;
        }
      }
    }
    fclose(fileSource);
    pthread_mutex_unlock(&mutex_channel_file);
    pthread_mutex_unlock(&mutex_channel_place);
  }
}

/**
 * @brief Initialise les sémaphores, tableaux de threads et autres variables nécessaire pour gérer les demandes liées aux fichiers
 * 
 * @param port Port sur lequel les sockets vont se connecter
 */
void initFiles(int port_file) {
  //On initialise le sémaphore indiquant le nombre de place restante
  if(sem_init(&sem_place_files, 0, MAX_FILES) == 1){
    perror("Erreur init sémaphore");exit(1);
  }

  dSFile = createSocketServer(port_file);
  puts("Socket file créée");

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

/**
 * @brief Initialise les sémaphores, tableaux de threads et autres variables nécessaire pour gérer la connexion de clients
 * 
 * @param port Port sur lequel les sockets vont se connecter
 */
void initClients(int port) {
  //On initialise le sémaphore indiquant le nombre de place restante
  if(sem_init(&sem_place, 0, MAX_CLIENTS) == 1){
    perror("Erreur init sémaphore");exit(1);
  }

  // Lancement du serveur
  dS = createSocketServer(port);
  puts("Socket client créée");

  // Liste qui contiendra les informations des clients
  clients = (struct clientStruct**)malloc(MAX_CLIENTS*sizeof(struct clientStruct *));
  for(int i=0; i<MAX_CLIENTS; i++) {
    clients[i] = NULL;
  }
  thread = (pthread_t*)malloc(MAX_CLIENTS*sizeof(pthread_t));

  // Thread qui s'occupera des threads clients zombies
  zombieStack = createStack();
  pthread_create(&thread_cleaner, NULL, cleanerClients, NULL);
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
  initChannels();
  initClients(port);

  pthread_create(&file_manager, NULL, acceptFiles, NULL);

  // CTRL-C
  signal(SIGINT, arret);
  acceptClients();

  stopServeur(dS);
  exit(EXIT_SUCCESS);
}