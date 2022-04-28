all: serveur stack client
serveur : serveur.c 
	gcc -Wall -Wextra -o serveur serveur.c stack.c -pthread -lpthread
stack: stack.c
	gcc -Wall -Wextra -o stack stack.h
client : client.c
	gcc -Wall -Wextra -o client client.c -pthread -lpthread
