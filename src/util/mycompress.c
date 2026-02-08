/* mycompress.c */
/* $Id: mycompress.c,v 1.3 1993/08/16 01:57:45 nils Exp $ */

#include <stdio.h>
#include "externs.h"

int main(void)
{
  FILE *f;
  int j;
  char buf[1050];

  f = popen("compress", "r");
  if (f == NULL)
  {
    exit(123);
  }
  while ((j = fread(buf, 1, 1000, f)) > 0)
    if (j > fwrite(buf, 1, 1000, stdout))
    {
      pclose(f);
      exit(123);
    }
  pclose(f);
  exit(0);
}
