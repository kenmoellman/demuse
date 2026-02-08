
/* decompress.c */
/* $Id: decompress.c,v 1.3 1993/08/16 01:57:42 nils Exp $ */

#include <stdio.h>
#include "externs.h"

int main(void)
{
  char buf[16384];

  while (gets(buf))
  {
    puts(uncompress(buf));
  }
  exit(0);
}
