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
#define SIZE_PSEUDO 20
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
 * @brief Affiche un message en rouge
 * 
 * @param m Message
 */
void putsRed(char m[]) {
  printf("%s", "\e[1;31m");
  printf("%s", m);
  puts("\e[0m");
}
/**
 * @brief Affiche un message en bleu
 *
 * @param m Message
 */
void putsBlue(char m[]) {
  printf("%s", "\e[1;34m");
  printf("%s", m);
  puts("\e[0m");
}
/**
 * @brief Affiche un message en rose
 *
 * @param m Message
 */
void putsPink(char m[]) {
  printf("%s", "\e[1;95m");
  printf("%s", m);
  puts("\e[0m");
}
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
  sendMessage(dS, "@disconnect", "Erreur send disconnect");
  putsBlue("Fin du client");
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
 * @return 0 si le pseudo est correct, 1 si trop petit, 2 si trop grand, 3 s'il contient des blancs
 */
int verifPseudo(char pseudo[]) {
  int res = 0;
  int size = strlen(pseudo);
  //Si pseudo trop petit
  if(size <1 || pseudo[0] == '\n') {
    res = 1;
  }
  //Si pseudo trop grand (+1 car on a pas encore enlever le \n)
  else if(size > SIZE_PSEUDO+1) {
    res = 2;
  }
  //S'il ne commence pas par @
  else if(pseudo[0] == '@') {
    res = 3;
  }
  //S'il ne contient pas de blanc
  else {
    //Enlever \n à la fin du pseudo
    pseudo[strcspn(pseudo, "\n")] = 0;
    for(size_t i=0; i<strlen(pseudo); i++) {
      if(isblank(pseudo[i])>0) {
        res = 4;
        break;
      }
    }
  }

  return res;
}
/**
 * @brief Fonction qui demande à l'utilisateur un pseudo et vérifie sa conformité avant de l'envoyer au serveur
 * 
 * @param m Chaine de caractères où sera stocké le pseudo à envoyer
 */
void choosePseudo(char m[]) {
  // Choix du pseudo
  int verif = 0;
  do {
    putsPink("Choisissez un pseudo :");
    fgets(m, SIZE_MESSAGE, stdin);
  
    verif = verifPseudo(m);
    if(verif==1) {
      putsRed("Le pseudo est trop petit, réessayez :");
    }
    else if(verif == 2) {
      printf("\e[1;31mLe pseudo est trop grand (supérieur à %d), réessayez :\e[0m\n", SIZE_PSEUDO);
    }
    else if(verif == 3) {
      putsRed("Le pseudo ne doit pas commencer par @");
    }
    else if(verif == 4) {
      putsRed("Le pseudo ne doit pas contenir d'espace, réessayez :");
    }
  }
  while(verif != 0);
}

/**
 * @brief Thread nettoyant les threads zombie
 * 
 * @return void* 
 */
void* cleaner() {
  while(1) {
    // On attend qu'un thread file se ferme
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
    sendMessage(dSF, "RCV", "Erreur protocol file receive");
    //On attend la confirmation du serveur
    recvMessage(dSF, m, "Erreur file protocol");

    if(strcmp(m, "OK") == 0){
      // On envoie le nom du fichier
      sendMessage(dSF, f->filename, "Erreur send filename");
      // On attends que le serveur nous dis si le fichier n'existe pas
      recvMessage(dSF, m, "Erreur file filename");
      
      if(strcmp(m, "FileNotExists") == 0){
        printf("@rcvf : Le fichier %s n'existe pas dans le serveur.\n", f->filename);
      }
      else if(strcmp(m, "OK") == 0) {
        //On envoie un message pour dire : on est prêt !
        sendMessage(dSF, "READY","Erreur send READY");
        //On attend la taille du fichier demandé
        recvMessage(dSF, m, "Erreur receive size");
        int size = atoi(m);
        sendMessage(dSF,"OK","Erreur confirm receive size");
        //On attend un message de confirmation pour l'ouverture du fichier
        recvMessage(dSF, m, "Erreur receive confirmation fopen");

        if(strcmp(m, "ERR") == 0){
          putsRed("Erreur lors de la réception du fichier, veuillez réessayer");
        }
        else if(strcmp(m, "OK") == 0) {
          char path[SIZE_MESSAGE] = "./download_client/";
          strcat(path, f->filename);
          printf("path : %s\n",path);
          // Enregistrer le fichier :
          FILE *fp = fopen(path, "wb");
          if(fp == NULL) {
            sendMessage(dSF, "ERR", "Erreur send erreur openFile");
            putsRed("Erreur lors de la réception du fichier, veuillez réssayer");
          }
          else {
            sendMessage(dSF, "OK", "Erreur send debute reception");
            //On peut commencer la réception
            if(strcmp(m, "OK") == 0){
              int dataTotal = 0;
              int sizeToGet = size;
              do {
                sizeToGet = size - dataTotal > SIZE_MESSAGE ? SIZE_MESSAGE : size - dataTotal;
                if(sizeToGet > 0) {
                  char *data = (char*)malloc(sizeof(char)*sizeToGet);
                  int dataGet = recv(dSF, data, sizeof(char)*sizeToGet, 0);
                  if(dataGet == -1) {
                    perror("Erreur recv file data");exit(1);
                  }
                  fwrite(data, sizeof(data[0]), sizeToGet, fp);
                  sendMessage(dSF, "OK", "Erreur file confirm data");
                  free(data);
                  dataTotal += dataGet;
                }
              } while(sizeToGet > 0);
              if(size - dataTotal <= 0) {
                printf("\e[1;34mLe fichier %s a bien été enregistré dans le dossier /download_client de l'application\n", f->filename);
              }
            }
            fclose(fp);
          }
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

  if(stat(path, &st) == -1){
    putsRed("Erreur lors de la lecture du fichier. Veuillez réessayer.");
  }else{
    int size = st.st_size;
    char sizeString[10];
    sprintf(sizeString, "%d", size);

    //Création du socket
    int dSF = createSocket(ip, port_file);

    char m[SIZE_MESSAGE];
    //On attends la confirmation de connexion au serveur
    recvMessage(dSF, m, "Erreur file connexion");

    if(strcmp(m, "OK") == 0) {
      //On envoie qu'on souhaite déposer un fichier
      sendMessage(dSF, "SEND", "Erreur protocol file receive");
      //On attend la confirmation du serveur
      recvMessage(dSF, m, "Erreur file protocol");

      if(strcmp(m, "OK") == 0){
        // On envoie le nom du fichier
        sendMessage(dSF, f->filename, "Erreur send filename");
        // On attends que le serveur nous dis si un fichier n'existe pas déjà sous ce nom
        recvMessage(dSF, m, "Erreur file filename");
        if(strcmp(m, "ERR") == 0) {
          putsRed("Une erreur s'est produite sur le serveur, veuillez réessayer");
        }
        else if(strcmp(m, "OK") == 0) {
          // On envoie alors la taille, puis les données
          sendMessage(dSF, sizeString, "Erreur send size");
          recvMessage(dSF, m, "Erreur recv file OK");

          if(strcmp(m, "OK") == 0) {
            FILE * fp = fopen(path, "rb");
            //Gestion erreur fopen 
            if (fp == NULL){
              //On averti le serveur
              sendMessage(dSF,"ERR","Erreur sending erreur fopen");
              putsRed("Erreur d'ouverture du fichier, veuillez réessayer");
            }else{
              sendMessage(dSF,"OK","Erreur sending erreur fopen");

              int dataSent = 0;
              while(dataSent < size && strcmp(m, "OK") == 0) {
                int sizeToGet = size - dataSent > SIZE_MESSAGE ? SIZE_MESSAGE : size - dataSent;
                char *data = (char*)malloc(sizeof(char)*sizeToGet);
                for(int i=0; i<sizeToGet; i++) {
                  data[i] = 0;
                }
                dataSent += fread(data, sizeof(char), sizeToGet, fp);

                int sent = send(dSF, data, sizeof(char)*sizeToGet, 0);
                if(sent == -1) {
                  perror("Erreur send data file");exit(1);
                }
                recvMessage(dSF, m, "Erreur recv file OK");
                free(data);
              }
              if(strcmp(m, "OK") == 0) {
                printf("\e[1;34mLe fichier %s a bien été envoyé sur le serveur\n", f->filename);
              }
              else {
                putsRed("Une erreur s'est produite, le fichier n'a pas pu être envoyé");
              }
              fclose(fp);
            }
          }
        }
      }
    }
    close(dSF);
  }
  

  pthread_mutex_unlock(&mutex_file);

  pthread_mutex_lock(&mutex_thread_file);
  tabIndexThreadFile[f->numero] = 1;
  free(f->filename);
  free(f);
  pthread_mutex_unlock(&mutex_thread_file);
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

/**
 * @brief Demande à l'utilisateur de choisir un fichier à envoyer au serveur
 * 
 */
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
        putsPink("Veuillez entrer le numéro associé au fichier pour l'envoyer");
        putsPink(" 0 : Annuler l'envoi");
        rewinddir(dir);
        while ((ent = readdir(dir)) != NULL) {
          if(strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 ){
            tab_file[nb_file] = malloc(sizeof(char) * (strlen(ent->d_name) + 1));
            strcpy(tab_file[nb_file],ent->d_name);
            printf("\e[1;95m %d : %s \e[0m\n",nb_file + 1,tab_file[nb_file]);
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
          printf("\e[1;34mLe fichier sélectionné est : %s\e[0m\n",file_name);
          for(int i=0; i<nb_file; i++){
            free(tab_file[i]);
          }
          sendFile(file_name);
        }else{
          putsPink("Sélection annulée");
        }
      }else{
        putsRed("Aucun fichier dans le dossier \"download_client\"");
      }
    }else{
      perror("Erreur open download_client");exit(1);
    }
  }
  else {
    printf("\e[1;34mVous essayez déjà d'envoyer ou de télécharger le nombre maximum de fichiers (%d)\e[0m\n", MAX_FILES);
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
    m[SIZE_MESSAGE-1] = '\0';

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
        puts("");
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
      printf("%s", reception);
      puts("\e[0m");
    }
    if(strcmp(reception, "@d\n") == 0 || strcmp(reception, "@disconnect\n") ==0 || r==0){
      fini = 1;
    }
  } while(fini != 1);
}

/**
 * @brief Une fois le client correctement connecté et login, on initalise ce dont on aura besoin,
 * et lance les parties gérant les envoies et les réceptions
 * 
 * @param dS 
 */
void launchClient(int dS) {
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
/**
 * @brief Main
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */
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
  recvMessage(dS, m, "Erreur connexion au serveur");

  if(strcmp(m, "OK") == 0) {
    putsBlue("Connexion réussie");
    
    // Choix du pseudo
    choosePseudo(m);
    sendMessage(dS, m, "Erreur send Pseudo");
    recvMessage(dS, m, "Erreur recv Pseudo");

    // Si pseudo accepté
    if(strcmp(m, "OK") == 0) {
      putsBlue("Login réussi");
      launchClient(dS);
    }
    else if(strcmp(m, "PseudoTaken") == 0) {
      putsRed("Ce pseudo est déjà pris");
    }
  }
  else {
    putsRed("Le socket n'a pas pu se connecter au serveur");
  }

  exit(EXIT_SUCCESS);
}