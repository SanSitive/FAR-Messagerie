all: serveur client
serveur : serveur.c 
	gcc -Wall -Wextra -o serveur serveur.c -pthread -lpthread
client : client.c
	gcc -Wall -Wextra -o client client.c -pthread -lpthread
