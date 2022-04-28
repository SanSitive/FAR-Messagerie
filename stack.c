// Pile d'entier

#include "stack.h"

/**
 * @brief Créer une pile
 * 
 * @return Stack* 
 */
Stack * createStack() {
  Stack* s = (Stack*) malloc(sizeof(Stack));
  s->nb = 0;
  s->top = NULL;
  return s;
}

/**
 * @brief Vérifie si la pile est vide (1 si oui, 0 sinon)
 * 
 * @param stack
 * @return int 
 */
int emptyStack(Stack* stack) {
  return stack->nb == 0 ? 1 : 0;
}

/**
 * @brief Ajoute un entier à la pile
 * 
 * @param stack
 * @param element 
 */
void pushStack(Stack* stack, int element) {
  StackRecur* sr = (StackRecur *) malloc(sizeof(StackRecur));
  sr->element = element;
  sr->next = stack->top;
  stack->top = sr;
  stack->nb++;
}
/**
 * @brief Retourne le sommet de la pile, et le supprime de la pile
 *        Retourne -1 si la pile est vide
 * @param stack
 * @return int 
 */
int popStack(Stack* stack) {
  int result = -1;
  if(emptyStack(stack) == 0) {
    result = stack->top->element;
    StackRecur* sr = stack->top;
    stack->top = sr->next;
    free(sr);
    stack->nb--;
  }
  
  return result;
}

/**
 * @brief Vide la pile
 * 
* @param stack
 */
void clearStack(Stack* stack) {
  while(emptyStack(stack) == 0) {
    popStack(stack);
  }
}