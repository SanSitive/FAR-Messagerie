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

pthread_mutex_t mutex_file;

pthread_t * thread_sendFile;
sem_t sem_place_files;          //Sémaphore indiquant le nombre de demande de fichier restant

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
 */
void sendMessage(int dS, char msg[], char erreur[]) {
  if(-1 == send(dS, msg, strlen(msg)+1, 0)) {
    perror(erreur);exit(1);
  }
}

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

  for (int i=0;i<MAX_FILES;i++){
    pthread_cancel(thread_sendFile[i]);
  }
  free(thread_sendFile);
  pthread_cancel(thread_cleaner); // Attends la fin du processus d'envoie de fichier

  sendMessage(dS, "@disconnect", "Erreur send disconnect"); // Prévenir le serveur
  stopClient(dS); // Fermer la socket
  exit(EXIT_SUCCESS);
}

int verifPseudo(char pseudo[]) {
  int res = 1;
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
void * cleaner() {
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
  stat(fichier, &st);
  int size = st.st_size;
  char sizeString[10];
  sprintf(sizeString, "%d", size);
  printf("%s", sizeString);

  FILE * fp = fopen(fichier, "r");
  //@sf nom_fichier taille
  char msg[SIZE_MESSAGE];
  strcpy(msg, "@sf ");
  strcat(msg, fichier);
  strcat(msg, " ");
  strcat(msg, sizeString);
  //sendMessage(, msg, "Erreur send @sf");
  fclose(fp);

  pthread_mutex_unlock(&mutex_file);
  free(f->filename);
  free(f);
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

/**
 * @brief Envoie un fichier du dossier "download_client" au serveur
 * 
 * @param fichier 
 */
void sendFile(char fichier[]) {
  char * filename = (char*)malloc(strlen(fichier)+1);

  struct fileStruct * self = (struct fileStruct*) malloc(sizeof(struct fileStruct));
  self->filename = filename;
  self->numero = getEmptyPosition(thread_sendFile, MAX_FILES);

  pthread_create(&thread_files[self->numero], NULL, sendFileProcess, (void*)self);
}

void selectFile(){
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
        char file_name[strlen(tab_file[input - 1]) + 1];
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
    fgets(m, SIZE_MESSAGE, stdin);
    
    if(strlen(m) > 0) {
      if(strcmp(m, "@sendfile\n") == 0){
        selectFile();
      }else{
        s = send(dS, m, strlen(m)+1, 0);
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
    
    if(-1 == send(dS, m, strlen(m)+1, 0)) {
      perror("Erreur send Pseudo");exit(1);
    }
    if(-1 == recv(dS, m, sizeof(char)*SIZE_MESSAGE, 0)) {
      perror("Erreur recv Pseudo");exit(1);
    }

    // Si pseudo accepté
    if(strcmp(m, "OK") == 0) {
      puts("Login réussi");

      thread_sendFile = (pthread_t*)malloc(MAX_FILES*sizeof(pthread_t));

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