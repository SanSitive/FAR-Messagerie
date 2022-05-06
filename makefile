all: serveur client stack
serveur : serveur.c 
	gcc -Wall -Wextra -o serveur serveur.c stack.c -pthread -lpthread
client : client.c
	gcc -Wall -Wextra -o client client.c stack.c -pthread -lpthread
stack: stack.c
	gcc -Wall -Wextra -o stack stack.h
