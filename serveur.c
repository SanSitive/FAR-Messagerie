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
int numberOfChannels;
pthread_mutex_t mutex_channel;

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
 * @brief Enregistre les channels dans le fichier channel.txt
 * 
 */
void saveChannels() {
  FILE* fp = fopen("channel.txt", "w");
  if(fp != NULL) {
    for(int i=0; i<MAX_CHANNEL; i++) {
      if(channels[i] != NULL) {
        if(channels[i]->name != NULL){
          fwrite(channels[i]->name, sizeof(channels[i]->name[0]), strlen(channels[i]->name), fp);
          fwrite("\n", sizeof(char), strlen("\n"), fp);
        }
      }
    }
    fwrite("END", sizeof(char), 3, fp);
    fclose(fp);
  }
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

  //Sauvegarde des channels, et free
  pthread_mutex_lock(&mutex_channel);
  saveChannels();
  for(int i=0; i<MAX_CHANNEL; i++) {
    if(channels[i] != NULL) {
      if(channels[i]->name != NULL){
        free(channels[i]->name);
      }
      if(channels[i]->description != NULL){
        free(channels[i]->description);
      }
      free(channels[i]);
    }
  }
  free(channels);
  pthread_mutex_unlock(&mutex_channel);

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
  pthread_mutex_destroy(&mutex_channel);
  sem_destroy(&sem_place);
  sem_destroy(&sem_thread);
  sem_destroy(&sem_place_files);
  sem_destroy(&sem_thread_files);


  //Clear les piles des cleaners
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
        if(strcmp(pseudo, clients[i]->pseudo) == 0) {//Pseudo trouvé
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
 * @brief Vérifie si un nom de channel est déjà pris. Retourn 1 si oui, 0 sinon
 * 
 * @param name
 * @return int 
 */
int isChannelNameTaken(char name[]) {
  int res = 0;
  for(int i=0; i<MAX_CHANNEL; i++) {
    if(channels[i] != NULL) {
      if(channels[i]->name != NULL) {
        if(strcmp(name, channels[i]->name) == 0) {//Nom du channel trouvé
          res = 1;
          break;
        }
      }
    }
  }
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
  pthread_mutex_lock(&mutex_channel);
  strcpy(msgPseudo, "[\e[1;33m");
  if(p->channel != NULL)
    strcat(msgPseudo, p->channel->name);
  else
    strcat(msgPseudo, "Général");
  strcat(msgPseudo, "\e[0m] \e[1;36m");
  strcat(msgPseudo, p->pseudo);
  pthread_mutex_unlock(&mutex_clients);
  pthread_mutex_unlock(&mutex_channel);
  strcat(msgPseudo, "\e[0m : \e[0;37m");
  int sizeMsgPseudo = strlen(msgPseudo);
  int sizeMsg = strlen(msg) + 1;

  int toSend = sizeMsg;
  int nbSend = 0;
  //Tant qu'on a pas envoyé le message en entier
  while(toSend > nbSend) {
    char msgToSend[SIZE_MESSAGE];
    strncpy(msgToSend, msgPseudo, sizeMsgPseudo);
    msgToSend[sizeMsgPseudo] = '\0';
    //Si ce qu'il reste à envoyer dépasse la taille disponible (taille totale disponible - la taille que prend l'entête qu'on a ajouté)
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
    //On envoie le message aux autres clients
    pthread_mutex_lock(&mutex_clients);
    for(int i = 0; i<MAX_CLIENTS; i++) {
      if(clients[i] != NULL) {
        if(clients[i]->channel == p->channel){
          if(p->dSC != clients[i]->dSC && clients[i]->pseudo != NULL) {
            sendMessage(clients[i]->dSC, msgToSend, "Erreur send clientToAll");
          }
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
    fclose(fileSource);
  }
  pthread_mutex_unlock(&mutex_help);
}

/**
 * @brief Envoie la liste des utilisateurs (sauf lui-même) au client en paramètre
 * 
 * @param dSC 
 */
void listClients(int dSC) {
  sendMessage(dSC, "Pseudo              | Salon", "Erreur send entête listClients");
  pthread_mutex_lock(&mutex_clients);
  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i] != NULL) {
      if(clients[i]->pseudo != NULL) {
        char msg[SIZE_MESSAGE] = "";
        strcat(msg, clients[i]->pseudo);
        int size = strlen(clients[i]->pseudo);
        for(int i=size; i<20; i++)
          strcat(msg, " ");
        strcat(msg, "| ");
        if(clients[i]->channel == NULL) {
          strcat(msg, "General");
        }
        else {
          pthread_mutex_lock(&mutex_channel);
          strcat(msg, clients[i]->channel->name);
          pthread_mutex_unlock(&mutex_channel);
        }
        sendMessage(dSC, msg, "Erreur send listClients");
      }
    }
  }
  pthread_mutex_unlock(&mutex_clients);
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
  int debutP = -1;
  while(debutP == -1 && j<taillePM){
    if (!(isblank(pseudoMessage[j])>0)){
      debutP = j;
    }
    j++;
  }
  int finP = 0 ;
  while(finP == 0 && j<taillePM){
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
  
  if(debutP != -1 && finP != 0 && debutM != 0 && debutM != (int)strlen(pseudoMessage)){//Le format de la commande a été respecté
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
  }else{
    sendMessage(p->dSC,"Le format de la commande n'est pas respecté", "Erreur send bad format");
  }
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
 * @brief Fonction qui permet d'afficher tous les channels 
 *        (Envoi successivement chacun des channels disponibles)
 * 
 * @param dSC 
 */
void allChannels(int dSC){
  pthread_mutex_lock(&mutex_channel);
  for(int i =0; i<MAX_CHANNEL; i++){
    if(channels[i] != NULL){
      char temp[strlen(channels[i]->name)];
      strcpy(temp,channels[i]->name);
      char msg[SIZE_MESSAGE];
      sprintf(msg, "%s : %d/%d",temp,channels[i]->count,channels[i]->capacity);//Envoie le nom du channel et le nombre de personnes connectées
      sendMessage(dSC,msg,"Erreur envoi channel");
    }
  }
  pthread_mutex_unlock(&mutex_channel);
}

/**
 * @brief Fonction qui permet au client de rejoindre un channel
 * 
 * @param p 
 * @param msg 
 */
void joinChannel(struct clientStruct* p, char msg[]){
  //On créer une place pour le nom du channel que l'on veut rejoindre
  int tailleC= strlen(msg) - 4 + 1;
  char *nomChannel = (char*)malloc(tailleC);
  strncpy(nomChannel, msg + 4, tailleC);

  pthread_mutex_lock(&mutex_channel);
  pthread_mutex_lock(&mutex_clients);

  int found = 0;
  //On cherche dans la liste des channels, le channel que l'utilisateur a demandé
  for (int i = 0; i < MAX_CHANNEL; i++){
    if(channels[i] != NULL){
      if (strcmp(channels[i]->name, nomChannel) == 0 ){
        if(channels[i]->count < channels[i]->capacity){
          //On change le channel actuel du client
          if(p->channel != NULL){
            p->channel->count --;
          }
          p->channel = channels[i];
          p->channel->count ++;
          sendMessage(p->dSC, "Channel rejoint !", "Erreur sending join channel");
          found = 1;
          break;
        }else{
          found = 2;
        }
      }
    }
  }
  if(found == 0){//Si le channel n'est pas trouvé
    sendMessage(p->dSC,"Ce channel n'existe pas","Erreur sending channel not found");
  }else if (found == 2){//Si le channel est plein
    sendMessage(p->dSC,"Ce channel est plein","Erreur sending channel full");
  }
  

  pthread_mutex_unlock(&mutex_channel);
  pthread_mutex_unlock(&mutex_clients);

  free(nomChannel);
}

/**
 * @brief Fonction qui permet au client de sortir du channel où il se situe
 * 
 * @param p 
 */
void disconnectChannel(struct clientStruct* p){
  pthread_mutex_lock(&mutex_channel);
  pthread_mutex_lock(&mutex_clients);

  //Si le client est dans aucun channel
  if (p->channel == NULL){
    sendMessage(p->dSC, "Vous n'êtes pas dans un channel !", "Erreur sending user aren't in a channel");
  }else{
    p->channel->count--;
    p->channel = NULL;
    sendMessage(p->dSC, "Vous êtes sorti du channel! Retour au channel général!", "Erreur sending user is out of a channel");
  }
  
  pthread_mutex_unlock(&mutex_channel);
  pthread_mutex_unlock(&mutex_clients);
}

/**
 * @brief Permet d'obtenir la première place non occupé dans le tableau des channels
 *        Renvoie l'index de la position dans le tableau ou -1 si il n'est pas trouvé
 * 
 * @param tab Le tableau dans lequel on cherche la place null
 * @param taille 
 * @return int 
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
 * @brief Update le channel avec les paramètres contenu dans le message
 *        ex : message = @uc Salon_1 capacity 3 => modifie la proprieté capacity du channel nommé Salon_1
 * 
 * @param msg 
 */
void updateChannel(int dSC, char msg[]){
  //On créer une place le sous-string composé du message passé en paramètre sans les 3 caractères de la commande
  int tailleNAV = strlen(msg) - 3 + 1;
  char *nameAttributeValue = (char*)malloc(tailleNAV); //string = [::space::]*<name>[::space::]*<attribute>[::space::]*<description>
  strncpy(nameAttributeValue, msg + 3, tailleNAV);


  //On cherche délimite chacune des parties du name, attribute, value of attribute
  //en se servant des changements entre espaces et caractère non espace

  //On délimite les caractères du name au sein du message
  int j = 0;
  int debutN = -1;
  while(debutN == -1 && j<tailleNAV){
    if (!(isblank(nameAttributeValue[j])>0)){
      debutN = j;
    }
    j++;
  }
  int finN = 0 ;
  while(finN == 0 && j<tailleNAV){
    if (isblank(nameAttributeValue[j])>0){
      finN = j - 1;
    } 
    j++;
  }

  //On délimite les caractère de l'attribut au sein du message 
  int debutA = 0;
  while((debutA == 0) && j < tailleNAV){
    if (!(isblank(nameAttributeValue[j])>0)){
      debutA = j;
    }
    j++;
  }

  int finA = 0 ;
  while(finA == 0 && j<tailleNAV){
    if (isblank(nameAttributeValue[j])>0){
      finA = j - 1;
    } 
    j++;
  }

  //Cherche le début de la value dans le message
  //La fin = fin du message
  int debutV = 0;
  while((debutV == 0) && j < tailleNAV){
    if (!(isblank(nameAttributeValue[j])>0)){
      debutV = j;
      break;
    }
    j++;  
  }

  if(debutN != -1 && finN != 0 && debutA != 0 && finA != 0 && debutV != 0 && debutV != (int)strlen(nameAttributeValue)){//Respecte le bon format de command
    //On créer une place pour le name
    int tailleN = finN - debutN + 1;
    char *name = (char*)malloc(tailleN + 1);
    strncpy(name, nameAttributeValue + debutN, tailleN);
    name[tailleN] = '\0';
    
    //On créer une place pour l'attribute
    int tailleA = finA - debutA + 1;
    char *attribute = (char*)malloc(tailleA + 1);
    strncpy(attribute, nameAttributeValue + debutA, tailleA);
    attribute[tailleA] = '\0';
    

    //On créer une place pour la value
    int tailleV = (int)strlen(nameAttributeValue) - debutV + 1;
    char *value = (char*)malloc(tailleV + 1);
    strncpy(value, nameAttributeValue + debutV, tailleV);
    value[tailleV] = '\0';
    

    //On cherche le channel
    pthread_mutex_lock(&mutex_channel);
    int find = -1;
    for(int i = 0; i<MAX_CHANNEL; i++){
      if(channels[i] != NULL){
        if(strcmp(channels[i]->name, name) == 0){
          find = i;
          break;
        }
      }
    }
    //Si le channel à été trouvé
    if(find != -1){
      //On modifie la valeur souhaité
      if(strcmp(attribute,"capacity") == 0){
        channels[find]->capacity = atoi(value);
        free(value);
        sendMessage(dSC,"Capacité max du channel modifiée avec succès", "Erreur send confirmation update");  
      }else if (strcmp(attribute,"name") == 0){
        if(0 == isChannelNameTaken(value)){
          free(channels[find]->name);
          channels[find]->name = value;
          sendMessage(dSC,"Nom modifié avec succès", "Erreur send confirmation update");
        }else{
          sendMessage(dSC,"Nom de channel déjà utilisé","Erreur send @uc channel name used");
        }
      }else if(strcmp(attribute,"description") == 0){
        free(channels[find]->description);
        channels[find]->description = value;
        sendMessage(dSC,"Description modifiée avec succès", "Erreur send confirmation update");
      }
    }else{
      sendMessage(dSC,"Le channel n'a pas été trouvé", "Erreur send confirmation update");
    }
    pthread_mutex_unlock(&mutex_channel);
    //Ne pas oublier de free() si l'on a pas modifié les valeurs
    free(name);
    free(attribute);
  }else{
    sendMessage(dSC,"Le format de la commande n'est pas respecté","Erreur send @uc command format");
  }
}
/**
 * @brief Découpe le message contenant les informations d'un channel, puis en créé un en utilisant ces paramètres
 * 
 * @param msg Le message qui contient les informations de type @cc name capacity description 
 */
void addChannel(int dSC,char msg[]){
  //On créer une place le sous-string composé du message passé en paramètre sans les 3 caractères de la commande
  int tailleNCD = strlen(msg) - 3 + 1;
  char *nameCapacityDescription = (char*)malloc(tailleNCD);
  strncpy(nameCapacityDescription, msg + 3, tailleNCD);


  //On cherche délimite chacune des parties du name, capacity, description
  //en se servant des changements entre espaces et caractère non espace

  //On délimite les caractères du name au sein du message
  int j = 0;
  int debutN = 0;
  while(debutN == 0 && j<tailleNCD){
    if (!(isblank(nameCapacityDescription[j])>0)){
      debutN = j;
    }
    j++;
  }
  int finN = 0 ;
  while(finN == 0 && j<tailleNCD){
    if (isblank(nameCapacityDescription[j])>0){
      finN = j - 1;
    } 
    j++;
  }

  //On délimite les caractère de la capacity au sein du message 
  int debutC = 0;
  while((debutC == 0) && j < tailleNCD){
    if (!(isblank(nameCapacityDescription[j])>0)){
      debutC = j;
    }
    j++;
  }

  int finC = 0 ;
  while(finC == 0 && j < tailleNCD){
    if (isblank(nameCapacityDescription[j])>0){
      finC = j - 1;
    } 
    j++;
  }

  //Cherche le début de la description dans le message
  //La fin = fin du message
  int debutD = 0;
  while((debutD == 0) && j < tailleNCD){
    if (!(isblank(nameCapacityDescription[j])>0)){
      debutD = j;
    }
    j++;
  }

  if(debutD != 0 && finC !=0 && debutC != 0 && finN != 0 && debutD != (int)strlen(nameCapacityDescription)){//Si la string possède bien le format @cc name capacity description
    //On créer une place pour le name
    int tailleN = finN - debutN + 1;
    char *name = (char*)malloc(tailleN + 1);
    strncpy(name, nameCapacityDescription + debutN, tailleN);
    name[tailleN] = '\0';

    if(0 == isChannelNameTaken(name)){//Name n'est pas pris

      //On créer une place pour la capacité
      int tailleC = finC - debutC + 1;
      char *capacityChar = (char*)malloc(tailleC + 1);
      strncpy(capacityChar, nameCapacityDescription + debutC, tailleC);
      capacityChar[tailleC] = '\0';
      int capacity = atoi(capacityChar);

      //On créer une place pour la description
      int tailleD = strlen(nameCapacityDescription) - debutD + 1;
      char *description = (char*)malloc(tailleD + 1);
      strncpy(description, nameCapacityDescription + debutD, tailleD);
      description[tailleD] = '\0';

      //On initialise un channel que l'on sauvegarde dans le tableau des channels
      struct channelStruct * self = (struct channelStruct*) malloc(sizeof(struct channelStruct));
      self->capacity = capacity;
      self->count = 0;
      self->name = malloc((strlen(name)+1)*sizeof(char));
      strcpy(self->name,name);
      
      self->description = malloc((strlen(description)+1)*sizeof(char));
      strcpy(self->description,description);

      self->numero = getEmptyPositionChannels(channels,MAX_CHANNEL);
      
      channels[self->numero] = self;
      //On augmente le nombre actuel de channel
      numberOfChannels++;

      free(capacityChar);
      sendMessage(dSC,"Le channel à été créé avec succès","Erreur send channel successfully created");
    }else{
      sendMessage(dSC,"Le nom du channel est déjà pris","Erreur send channel name taken");
    }
  }else{//La string ne comporte pas le bon format
    sendMessage(dSC,"Le format de la commande n'est pas respecté","Erreur send error create chan");
  }

  free(nameCapacityDescription);
}
/**
 * @brief Crée un channel, envoie un message de réussite au client si le channel à bien été créer
 *        sinon envoie impossible
 * 
 * @param dSC 
 * @param msg 
 */
void createChannel(int dSC, char msg[]){
  pthread_mutex_lock(&mutex_channel);
  char reponse[SIZE_MESSAGE];
  if(numberOfChannels < MAX_CHANNEL) {   //Si le nombre de channel actuel permet d'en créer un nouveau
    addChannel(dSC,msg);
  }else{//Sinon
    strcpy(reponse,"Impossible de créer le channel, trop de channel");
    sendMessage(dSC,reponse,"Erreur send impossible to create channel");
  }
  pthread_mutex_unlock(&mutex_channel);


}
/**
 * @brief Supprime un channel
 * 
 * @param dSC Le dSC du client
 * @param msg Le message contenant le nom du channel
 */
void deleteChannel(int dSC, char msg[]){

  //On créer une place pour le nom du channel que l'on veut rejoindre
  int tailleC= strlen(msg) - 4 + 1;
  char *name = (char*)malloc(tailleC);
  strncpy(name, msg + 4, tailleC);
  
  int done = 0;
  pthread_mutex_lock(&mutex_channel);
  for(int i =0; i<MAX_CHANNEL; i++){
    if(channels[i] != NULL){
      if(strcmp(name,channels[i]->name) == 0){
        pthread_mutex_lock(&mutex_clients);
        for(int j =0; j<MAX_CLIENTS; j++){
          if(clients[i] != NULL){
            if(clients[j]->channel == channels[i]){//Si la personne est connecté au salon que l'on veut supprimer
              clients[j]->channel = NULL;
              sendMessage(clients[j]->dSC,"Le channel dans lequel vous êtes vient d'être supprimé\nVous rejoignez le channel général","Erreur envoi message delete chan");
            }
          }
        }
        pthread_mutex_unlock(&mutex_clients);
        //Libère la mémoire associé au channel
        free(channels[i]->name);
        free(channels[i]->description);
        free(channels[i]);
        channels[i] = NULL;
        sendMessage(dSC,"Le channel à été supprimé avec succès", "Erreur message confirmation suppression chan");
        done = 1;
        break;
      }
    }
  }
  if(done == 0){
    sendMessage(dSC,"Suppression annulé : le channel n'existe pas","Erreur send confirmation suppr chan");
  }
  pthread_mutex_unlock(&mutex_channel);

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
        //On envoie à tous les utilisateurs du même channel
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
        }//Liste des channels disponibles
        else if(strcmp(msg,"@channels") == 0){
          allChannels(dSC);
        }//Pour rejoindre un channel par son nom
        else if ((msg[1] == 'j' && msg[2] == 'c') && isblank(msg[3])>0 ){
          joinChannel(p, msg);
        }//Pour se déconnecter d'un channel => rejoindre le channel général
        else if(strcmp(msg,"@dchannel") == 0){
          disconnectChannel(p);
        }//Pour créer un channel
        else if(msg[0] == '@' && msg[1] == 'c' && msg[2] == 'c' && isblank(msg[3])>0){
          createChannel(dSC,msg);
        }else if(msg[0] == '@' && msg[1] == 's' && msg[2] == 'c'){
          deleteChannel(dSC,msg);
        }else if (msg[0] == '@' && msg[1] == 'u' && msg[2] == 'c'){
          updateChannel(dSC,msg); 
        }//Si la commande n'existe pas
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
    fclose(fp);
  }
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
          fclose(fp);
        }
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
    // On attend qu'un thread client se ferme
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
  
  pthread_mutex_lock(&mutex_file);
  self->numero = getEmptyPositionFile(files, MAX_FILES);
  pthread_mutex_unlock(&mutex_file);

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
  self->channel = NULL;

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
  numberOfChannels = 0;

  // Tableau des channels
  channels = (struct channelStruct**)malloc(MAX_CHANNEL*sizeof(struct channelStruct *));
  for(int i=0; i<MAX_CHANNEL; i++) {
    channels[i] = NULL;
  }

  pthread_mutex_lock(&mutex_channel);
  FILE *fileSource;
  fileSource = fopen("channel.txt", "r");

  if(fileSource != NULL){ //pas d'erreur
    char ch;
    char chan[SIZE_MESSAGE];
    int count = 0;
    int END = 0;
    while( (( ch = fgetc(fileSource) ) != EOF) &&  END == 0 ){
      strncat(chan,&ch,1);
      count++;

      if(strcmp(chan,"END") == 0){//Si c'est la dernière ligne du fichier
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
        numberOfChannels++;
        //Fin d'instanciation du salon
        strcpy(chan,"");
        count = 0;
      }
    }
    fclose(fileSource);
  }
  pthread_mutex_unlock(&mutex_channel);
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