// Pile d'entier

#include <stddef.h>
#include <stdlib.h>

typedef struct Stack Stack;
typedef struct StackRecur StackRecur;

struct StackRecur {
  int element;
  StackRecur* next;
};
struct Stack {
  int nb;
  StackRecur* top;
};


/**
 * @brief Créer une pile
 * 
 * @return Stack* 
 */
Stack* createStack();
/**
 * @brief Vérifie si la pile est vide (1 si oui, 0 sinon)
 * 
 * @param stack 
 * @return int 
 */
int emptyStack(Stack* stack);
/**
 * @brief Ajoute un entier à la pile
 * 
 * @param stack 
 * @param element 
 */
void pushStack(Stack* stack, int element);
/**
 * @brief Retourne le sommet de la pile, et le supprime de la pile
 * 
 * @param stack
 * @return int 
 */
int popStack(Stack* stack);

/**
 * @brief Vide la pile
 * 
* @param stack
 */
void clearStack(Stack* stack);
