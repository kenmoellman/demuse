/** Modified nalloc.c; uses a volatile stack that's freed at the end of each process_commands() **/

/* this code was originally adapted from the e-zine produced *
 * by Saruk and has been heavily modified since that time.   */

#include "externs.h"

MSTACK *mem_first = NULL;
MSTACK *mem_last = NULL;
size_t number_stack_blocks = 0;
size_t stack_size = 0;


void __free_hunk(MSTACK *);
void *stack_em_int(size_t, int, int);
char *stralloc_int(char *, int);

void *stack_em_fun(size_t size)
{
    return(stack_em_int(size, 200, 0));
}

void *stack_em(size_t size)
{
    return(stack_em_int(size, 1, 0));
}

void *stack_em_int(size_t size, int timer, int perm)
{
      MSTACK *mnew;
      
      mnew = (MSTACK *)malloc(sizeof(MSTACK)+1);
      mnew->ptr = malloc(size);
      mnew->size = size;
      mnew->timer = timer + 50;  /* add some padding in case */
      mnew->perm = perm; /* perm flag */
      mnew->next = NULL;;
      mnew->prev = mem_last;
      if (mem_last)
      {
        mem_last->next = mnew;
        mem_last = mnew;
      }
      else
      {
        mem_first = mem_last = mnew;
      }
      
      stack_size = stack_size + sizeof(MSTACK) + size + 1;
      number_stack_blocks++;
      return(mnew->ptr);
}

/* void __free_hunk(MSTACK *stack) */
void __free_hunk(MSTACK *m)
{
/*      MSTACK *m = NULL;

      m = stack;
*/

      if (m == mem_first)
      {
        mem_first = m->next;
        if(mem_first)
        {
          mem_first->prev = NULL;
        }
        else
        {
          mem_last = NULL;
        }
      }
      else if (m == mem_last)
      {
        mem_last = m->prev;
        if(mem_last)
        {
          mem_last->next = NULL;
        }
        else
        {
          mem_first = NULL;
        }
      }
      else
      {
        (m->prev)->next = m->next;
        (m->next)->prev = m->prev;
      }

      if(m->ptr)
      {
              free(m->ptr);
      }
      stack_size = stack_size - m->size - sizeof(MSTACK);
      m->next = NULL;
      m->prev = NULL;
      free(m);

      number_stack_blocks--;
}

void clear_stack(void)
{
    MSTACK *m, *mnext; 

    if(!mem_first)
        return;

    m = mem_first;
              
    for(; m; m=mnext) 
    {
        mnext = m->next;
        m->timer--;
        if ((m->timer < 1) && (m->perm == 0))
        {
            __free_hunk(m);
        }
        m = mnext;
    }
}

char *stralloc(const char *string)
{
   return stralloc_int(string, 0);
}

char *stralloc_p(char *string)
{
   return stralloc_int(string, 1);
}

char *stralloc_int(char *string, int perm)
{
/*
    char new[65535], temp[65535];  * if anything more than 64K comes through, we're screwed. *
    char *s, *t, *p;

    strncpy(temp, string, 65530);
    temp[65530]='\0';

    t = s = temp;  

    strncpy(new,t,65530);
    new[65530]='\0';

    p = stack_em_int((strlen(new) + 1), 1, perm);
    strcpy(p, new);
*/

  char *p;

  p = stack_em_int((strlen(string) + 1), 1, perm);
  strcpy(p, string);
      
  return(p);
}


char *funalloc(char *string)
{
    char *p = stack_em_int((strlen(string) + 1), 5, 0);  /* permanantly allocate functions? */
    strcpy(p, string);
      
    return(p);
}

void strfree_p(char *string)
{
  MSTACK *m;

  for (m = mem_first; m; m=m->next)
  {
    if (m->ptr == string) /* if it's the same pointer */
    {
      __free_hunk(m);
      break;
    }
  } 
}


void shutdown_stack(void)
{
/*
  MSTACK *m, *mnext;

  for (m = mem_first; m; m=mnext)
  {
    mnext = m;
    __free_hunk(m);
  }
*/

  int x;

  for (x=0;x<10000;x++)
  {
    clear_stack();
  }

}
