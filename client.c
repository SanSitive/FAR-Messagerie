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
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>

#include "stack.h"

#define SIZE_MESSAGE 256
#define MAX_FILES 3 // Nombre de fichiers qu'on peut envoyer simultanément
int dS;
char * ip;
int port;
int port_file;

pthread_mutex_t mutex_file;
pthread_mutex_t mutex_thread_file;

pthread_t * thread_files;
sem_t sem_place_files;          //Sémaphore indiquant le nombre de demande de fichier restant
int * tabIndexThreadFile;       // Tableau servant à savoir quels index dans le tableau de thread de file sont disponibles

pthread_t thread_cleaner_files; //Thread cleaner des threads file zombie
sem_t sem_thread_files;         //Sémaphore indiquant le nombre de thread file zombie à nettoyer
Stack * zombieStackFiles;       //Pile d'entier contenant les index des threads files zombies

struct fileStruct {
  char * filename;
  int numero;
};

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
  int size = strlen(msg)+1;
  for(int i = size+1; i<SIZE_MESSAGE; i++) {
    msg[i] = 0;
  }
  if((r = send(dS, msg, SIZE_MESSAGE, 0)) == -1 ) {
    perror(erreur);exit(1);
  }
  return r;
}
int sendConstantMessage(int dS, char msg[], char erreur[]) {
  int r = 0;
  int size = strlen(msg)+1;
  if((r = send(dS, msg, size, 0)) == -1 ) {
    perror(erreur);exit(1);
  }
  return r;
}

/**
 * @brief Fonction pour recevoir les messages
 * 
 * @param dS 
 * @param msg 
 * @param erreur 
 * @return Nombre d'octets reçu
 */
int recvMessage(int dS, char msg[], char erreur[]) {
  int r = 0;
  if((r = recv(dS, msg, sizeof(char)*SIZE_MESSAGE, 0)) == -1) {
    perror(erreur);exit(1);
  }
  return r;
}
/**
 * @brief Renvoie une socket créée à partir d'une adresse ip et d'un port
 * 
 * @return int 
 */
int createSocket(char * ip, int port) {
  int dS = socket(PF_INET, SOCK_STREAM, 0);
  if(dS == -1) {
    perror("Erreur socket dSF");exit(1);
  }

  struct sockaddr_in aS;
  aS.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &(aS.sin_addr));
  aS.sin_port = htons(port);
  socklen_t lgA = sizeof(struct sockaddr_in);
  if(-1 == connect(dS, (struct sockaddr *) &aS, lgA)) {
    perror("Erreur connect dSF");exit(1);
  }
  return dS;
}

/**
 * @brief Ferme le socket client
 * 
 * @param dS 
 */
void stopClient(int dS) {
  // Ferme les threads lié à l'upload de fichiers
  for (int i=0;i<MAX_FILES;i++){
    pthread_cancel(thread_files[i]);
  }
  free(thread_files);
  free(tabIndexThreadFile);
  pthread_cancel(thread_cleaner_files);
  pthread_mutex_destroy(&mutex_file);
  pthread_mutex_destroy(&mutex_thread_file);

  // Préviens le serveur
  sendConstantMessage(dS, "@disconnect", "Erreur send disconnect");
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
  stopClient(dS); // Fermer la socket
  exit(EXIT_SUCCESS);
}

/**
 * @brief Vérifie que le pseudo est correctement écrit, c'est-à-dire sans espace ou retour à la ligne
 * 
 * @param pseudo char[]
 * @return int 1 si pseudo correct, 0 sinon
 */
int verifPseudo(char pseudo[]) {
  int res = 1;
  if(strlen(pseudo) == 1 && pseudo[0] == '\n') {
    return 0;
  }

  //Enlever \n à la fin du pseudo
  pseudo[strcspn(pseudo, "\n")] = 0;
  for(size_t i=0; i<strlen(pseudo); i++) {
    if(isblank(pseudo[i])>0) {
      res = 0;
      break;
    }
  }

  return res;
}

/**
 * @brief Thread nettoyant les threads zombie
 * 
 * @return void* 
 */
void* cleaner() {
  while(1) {
    // On attends qu'un thread file se ferme
    if(sem_wait(&sem_thread_files) == 1){
      perror("Erreur wait sémaphore client");exit(1);
    }
    pthread_mutex_lock(&mutex_thread_file);
    int place = popStack(zombieStackFiles);
    void *valrep;
    int rep = pthread_join(thread_files[place], &valrep);

    if (valrep == PTHREAD_CANCELED)
      printf("The thread was canceled - ");
    switch (rep) {
      case 0:
        printf("Thread %d a été joinned \n", place);
        break;
      default:
        printf("Erreur durant le join du thread : %d\n",place);
    }
    pthread_mutex_unlock(&mutex_thread_file);
  }
}

/**
 * @brief Processus gérant la reception d'un fichier du dossier "dowload_server" dans le dossier "dowload_client"
 * 
 * @return void* 
 */
void* receiveFileProcess(void * parametres){
  struct fileStruct * f = (struct fileStruct *) parametres;

  //Création de la socket
  int dSF = createSocket(ip, port_file);

  char m[SIZE_MESSAGE];
  //On attends la confirmation de connexion au serveur
  recvMessage(dSF, m, "Erreur file connexion");

  if(strcmp(m, "OK") == 0) {
    //On envoie qu'on souhaite récupérer un fichier
    sendConstantMessage(dSF, "RCV", "Erreur protocol file receive");
    //On attend la confirmation du serveur
    recvMessage(dSF, m, "Erreur file protocol");

    if(strcmp(m, "OK") == 0){
      // On envoie le nom du fichier
      sendMessage(dSF, f->filename, "Erreur send filename");
      // On attends que le serveur nous dis si le fichier n'existe pas
      recvMessage(dSF, m, "Erreur file filename");

      if(strcmp(m, "OK") == 0) {
        //On envoie un message pour dire : on est prêt !
        sendConstantMessage(dSF, "READY","Erreur send READY");
        //On attend la taille du fichier demandé
        recvMessage(dSF, m, "Erreur receive size");
        printf("size : %s \n",m);
        sendConstantMessage(dSF,"OK","Erreur confirm receive size");
        //On récupère la taille du fichier
        int size = atoi(m);
        char buffer[size];
        strcpy(buffer,"");
        int dataTotal = 0;
        int sizeToGet = size;
        do {
          sizeToGet = size - dataTotal > SIZE_MESSAGE ? SIZE_MESSAGE : size - dataTotal;
          if(sizeToGet > 0) {
            char data[sizeToGet+1];
            int dataGet = recv(dSF, data, sizeof(char)*sizeToGet, 0);
            if(dataGet == -1) {
              perror("Erreur recv file data");exit(1);
            }
            data[sizeToGet] = '\0';
            dataTotal += dataGet;
            strcat(buffer, data);
            sendConstantMessage(dSF, "OK", "Erreur file confirm data");
          }
        } while(sizeToGet > 0);

        recvMessage(dSF, m, "Erreur recv file END");
        if(strcmp(m, "END") == 0) {
          char path[SIZE_MESSAGE] = "./download_client/";
          strcat(path, f->filename);
          printf("path : %s\n",path);
          // Enregistrer le fichier :
          FILE *fp = fopen(path, "wb"); // must use binary mode
          fwrite(buffer, sizeof(buffer[0]), size, fp); // writes an array of doubles
          fclose(fp);
        }

      }else if(strcmp(m, "FileNotExists") == 0){
        printf("@rcvf : Le fichier %s n'existe pas dans le serveur.\n", f->filename);
      }
    }
    
  }

  pthread_mutex_unlock(&mutex_file);

  pthread_mutex_lock(&mutex_thread_file);
  tabIndexThreadFile[f->numero] = 1;
  free(f->filename);
  free(f);
  pthread_mutex_unlock(&mutex_thread_file);
  close(dSF);
  pthread_exit(0);
}

/**
 * @brief Processus gérant l'envoie d'un fichier du dossier "download_client"au serveur
 * 
 * @param filename
 */
void* sendFileProcess(void * parametres) {
  struct fileStruct * f = (struct fileStruct *) parametres;
  pthread_mutex_lock(&mutex_file);

  struct stat st;
  char path[SIZE_MESSAGE] = "./download_client/";
  strcat(path, f->filename);
  stat(path, &st);
  int size = st.st_size;
  char sizeString[10];
  sprintf(sizeString, "%d", size);

  //Création du socket
  int dSF = socket(PF_INET, SOCK_STREAM, 0);
  if(dSF == -1) {
    perror("Erreur socket dSF");exit(1);
  }

  struct sockaddr_in aS;
  aS.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &(aS.sin_addr));
  aS.sin_port = htons(port_file);
  socklen_t lgA = sizeof(struct sockaddr_in);
  if(-1 == connect(dSF, (struct sockaddr *) &aS, lgA)) {
    perror("Erreur connect dSF");exit(1);
  }

  char m[SIZE_MESSAGE];
  //On attends la confirmation de connexion au serveur
  recvMessage(dSF, m, "Erreur file connexion");

  if(strcmp(m, "OK") == 0) {
    //On envoie qu'on souhaite déposer un fichier
    sendConstantMessage(dSF, "SEND", "Erreur protocol file receive");
    //On attend la confirmation du serveur
    recvMessage(dSF, m, "Erreur file protocol");

    if(strcmp(m, "OK") == 0){
      // On envoie le nom du fichier
      sendMessage(dSF, f->filename, "Erreur send filename");
      // On attends que le serveur nous dis si un fichier n'existe pas déjà sous ce nom
      recvMessage(dSF, m, "Erreur file filename");

      if(strcmp(m, "OK") == 0) {
        // On envoie alors la taille, puis les données
        sendMessage(dSF, sizeString, "Erreur send size");
        recvMessage(dSF, m, "Erreur recv file OK");

        if(strcmp(m, "OK") == 0) {
          FILE * fp = fopen(path, "rb");
          int dataSent = 0;
          
          while(dataSent < size && strcmp(m, "OK") == 0) {
            int sizeToGet = size - dataSent > SIZE_MESSAGE ? SIZE_MESSAGE : size - dataSent;
            char data[sizeToGet];
            dataSent += fread(data, sizeof(char), sizeToGet, fp);

            int sent = send(dSF, data, sizeof(char)*sizeToGet, 0);
            if(sent == -1 ) {
              perror("Erreur send data file");exit(1);
            }
            recvMessage(dSF, m, "Erreur recv file OK");
          }
          sendConstantMessage(dSF, "END", "Erreur send file end");
          /*else { // error handling
            if (feof(fp))
                printf("Error reading test.bin: unexpected end of file\n");
            else if (ferror(fp)) {
                perror("Error reading test.bin");
            }
          }*/
          fclose(fp);
        }
      }
    }
  }

  pthread_mutex_unlock(&mutex_file);

  pthread_mutex_lock(&mutex_thread_file);
  tabIndexThreadFile[f->numero] = 1;
  free(f->filename);
  free(f);
  pthread_mutex_unlock(&mutex_thread_file);
  close(dSF);
  pthread_exit(0);
}

/**
 * @brief Trouve la première place libre dans le tableau, -1 si une place n'a pas été trouvée
 * 
 * @param tab 
 * @param taille 
 * @return int 
 */
int getEmptyPosition(int tab[], int taille) {
  int p = -1;
  for(int i=0; i<taille; i++) {
    if(tab[i] == 1) {
      p = i;
      break;
    }
  }
  return p;
}

/**
 * @brief Va chercher un fichier du serveur demandé par le client et l'ajoute dans son répertoire personnel
 * 
 * @param m 
 */
void receiveFile(char m[]){
  //Récupération du nom du fichier voulu
  int i = 5;
  while(isblank(m[i])>0){
    i++;
  }
  //On enlève les espaces à la fin
  int j = strlen(m);
  while(isblank(m[j])>0){
    j--;
  }
  //On créé un espace pour le nom du fichier et on le copie dedans
  int taille = j - i + 1;
  char *nomFichier = (char*)malloc(taille);
  strncpy(nomFichier, m + i, taille);
  //Enlever \n à la fin du nom du fichier
  nomFichier[strcspn(nomFichier, "\n")] = 0;

  //Création du thread
  struct fileStruct * self = (struct fileStruct*) malloc(sizeof(struct fileStruct));
  self->filename = nomFichier;

  pthread_mutex_lock(&mutex_thread_file);
  self->numero = getEmptyPosition(tabIndexThreadFile, MAX_FILES);
  tabIndexThreadFile[self->numero] = 0;
  pthread_mutex_unlock(&mutex_thread_file);

  pthread_create(&thread_files[self->numero], NULL, receiveFileProcess, (void*)self);
}

/**
 * @brief Envoie un fichier du dossier "download_client" au serveur
 * 
 * @param fichier 
 */
void sendFile(char *filename) {
  struct fileStruct * self = (struct fileStruct*) malloc(sizeof(struct fileStruct));
  self->filename = filename;

  pthread_mutex_lock(&mutex_thread_file);
  self->numero = getEmptyPosition(tabIndexThreadFile, MAX_FILES);
  tabIndexThreadFile[self->numero] = 0;
  pthread_mutex_unlock(&mutex_thread_file);

  pthread_create(&thread_files[self->numero], NULL, sendFileProcess, (void*)self);
}

void selectFile(){
  if(getEmptyPosition(tabIndexThreadFile, MAX_FILES) > -1) {
    struct stat st = {0};
    if(stat("./download_client", &st) == -1) {
      mkdir("./download_client", 0700);
    }
    int nb_file = 0;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("./download_client")) != NULL) {
      while ((ent = readdir(dir)) != NULL) {
        if(strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 ){
          nb_file++;
        }
      }
      if(nb_file > 0){
        char *tab_file[nb_file];
        nb_file = 0;
        puts("Veuillez entrer le numéro associé au fichier pour l'envoyer");
        puts(" 0 : Annuler l'envoi");
        rewinddir(dir);
        while ((ent = readdir(dir)) != NULL) {
          if(strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 ){
            tab_file[nb_file] = malloc(sizeof(char) * (strlen(ent->d_name) + 1));
            strcpy(tab_file[nb_file],ent->d_name);
            printf(" %d : %s \n",nb_file + 1,tab_file[nb_file]);
            nb_file++;
          }
        }
        closedir(dir);
        char m[5];
        fgets(m, 5, stdin);
        int input = atoi(m);
        if(input > 0 && input <= nb_file){
          char *file_name = (char*) malloc(strlen(tab_file[input - 1]) + 1);
          strcpy(file_name, tab_file[input - 1]);
          printf("Le fichier sélectionné est : %s\n",file_name);
          for(int i=0; i<nb_file; i++){
            free(tab_file[i]);
          }
          sendFile(file_name);
        }else{
          puts("Sélection annulée");
        }
      }else{
        puts("Aucun fichier dans le dossier \"download_client\"");
      }
    }else{
      perror("Erreur open download_client");exit(1);
    }
  }
  else {
    printf("Vous essayez déjà d'envoyer le nombre maximum de fichiers (%d)\n", MAX_FILES);
  }
}

/**
 * @brief Gère les entrées de l'utilisateur pour envoyer au serveur
 * 
 * @param dS 
 * @param taille 
 */
void pereSend(int dS) {
  char m[SIZE_MESSAGE];
  int s = 1;
  do {
    fgets(m, SIZE_MESSAGE-1, stdin);
    printf("Message avant : %s\n", m);
    printf("Taille avant : %d\n", strlen(m)+1);
    m[SIZE_MESSAGE-1] = '\0';
    printf("Message après : %s\n", m);
    printf("Taille après : %d\n", strlen(m)+1);
    if(strlen(m) > 1) {
      //Commande pour envoyer un fichier client dans le serveur
      if(strcmp(m, "@sendfile\n") == 0){
        selectFile();
      }
      //Commande pour récupérer un fichier du serveur dans l'espace client
      else if (m[1] == 'r' && m[2] == 'c' && m[3]=='v' && m[4]=='f' && isblank(m[5]) > 0){
        receiveFile(m);
      }
      else{
        s = sendMessage(dS, m, "Erreur send pere");
        if(-1 == s) {
          perror("Erreur send");exit(1);
        }
        // Non déconnecté
        else if(s != 0) {
          puts("Message Envoyé");
        }
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
  int fini = 0;
  do {
    char reception[SIZE_MESSAGE];
    int r = recvMessage(dS, reception, "Erreur recv");
    if(strcmp(reception, "@shutdown") == 0) {
      break;
    }
    // Non déconnecté
    else if(r != 0) {
      printf("Taille : %d\n", strlen(reception)+1);
      puts(reception);
    }
    if(strcmp(reception, "@d\n") == 0 || strcmp(reception, "@disconnect\n") ==0 || r==0){
      fini = 1;
    }
  } while(fini != 1);
}

int main(int argc, char *argv[]) {

  if(argc != 3){
    puts("Lancement : ./client ip port");
    exit(1);
  }

  ip = argv[1];
  port = atoi(argv[2]);
  port_file = port + 100;

  //Création du client
  dS = createSocket(ip, port);

  char m[SIZE_MESSAGE];

  //On attends la confirmation de connexion au serveur
  if(-1 == recv(dS, m, sizeof(char)*SIZE_MESSAGE, 0)) {
    perror("Erreur connexion au serveur");exit(1);
  }

  if(strcmp(m, "OK") == 0) {
    puts("Connexion réussie");

    // Choix du pseudo
    int verif = 0;
    do {
      puts("Choisissez un pseudo :");
      fgets(m, SIZE_MESSAGE, stdin);
    
      verif = verifPseudo(m);
      if(verif == 0) {
        puts("Le pseudo ne doit pas contenir d'espace, réessayez :");
      }
    }
    while(verif == 0);
    
    sendMessage(dS, m, "Erreur send Pseudo");
    recvMessage(dS, m, "Erreur recv Pseudo");

    // Si pseudo accepté
    if(strcmp(m, "OK") == 0) {
      puts("Login réussi");

      thread_files = (pthread_t*)malloc(MAX_FILES*sizeof(pthread_t));
      tabIndexThreadFile = (int*)malloc(MAX_FILES*sizeof(int));
      for(int i=0; i<MAX_FILES; i++) {
        tabIndexThreadFile[i] = 1;
      }

      pid_t pid;
      // Fork pour que l'un gère l'envoie, l'autre la réception
      pid = fork();
      if (pid != 0) { // PERE
        signal(SIGINT, arret);
        pereSend(dS);
        kill(pid, SIGINT);
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
    puts("Le socket n'a pas pu se connecter au serveur");
  }

  exit(EXIT_SUCCESS);
}