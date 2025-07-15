#include "config.h"
#include "externs.h"
#include "sock.h"
#include <ctype.h>

char *get_day(day)
  time_t day;
{ 
  struct tm *tim = localtime(&day);

  switch(tim->tm_wday)
  { 
    case 0:
      return(tprintf("Sunday"));
    case 1:
      return(tprintf("Monday"));
    case 2:
      return(tprintf("Tuesday"));
    case 3:
      return(tprintf("Wednesday"));
    case 4:
      return(tprintf("Thursday"));
    case 5:
      return(tprintf("Friday"));
    case 6:
      return(tprintf("Saturday"));
  }
  return("NULL");
}

char *mil_to_stndrd(day)
  time_t day;
{ 
  struct tm *tim = localtime(&day);
  
  if((tim->tm_hour/12) == 1)
    return(tprintf("pm"));
  return(tprintf("am"));
}

char *time_format_1(dt)
time_t dt;
{
  struct tm *delta;
  char buf[64];

  if (dt < 0)
    dt = 0;

  delta = gmtime(&dt);
  if (delta->tm_yday > 0)
  {
    if (delta->tm_yday >= 7)
      sprintf(buf, "%dw %02d:%02d",
	      (delta->tm_yday / 7), delta->tm_hour, delta->tm_min);
    else
      sprintf(buf, "%dd %02d:%02d",
	      delta->tm_yday, delta->tm_hour, delta->tm_min);
  }
  else
  {
    sprintf(buf, "%02d:%02d",
	    delta->tm_hour, delta->tm_min);
  }
  return stralloc(buf);
}

char *time_format_2(dt)
time_t dt;
{
  register struct tm *delta;
  char buf[64];

  if (dt < 0)
    dt = 0;

  delta = gmtime(&dt);
  if (delta->tm_yday > 0)
  {
    if (delta->tm_yday >= 7)
      sprintf(buf, "%dw", (delta->tm_yday / 7));
    else
      sprintf(buf, "%dd", delta->tm_yday);
  }
  else if (delta->tm_hour > 0)
  {
    sprintf(buf, "%dh", delta->tm_hour);
  }
  else if (delta->tm_min > 0)
  {
    sprintf(buf, "%dm", delta->tm_min);
  }
  else
  {
    sprintf(buf, "%ds", delta->tm_sec);
  }
  return stralloc(buf);
}

char *time_format_3(dt)
time_t dt;
{
  register struct tm *delta;
  char buf[64];

  if (dt < 0)
    dt = 0;

  delta = gmtime(&dt);
  if (delta->tm_yday > 0)
  {
    if (delta->tm_yday == 1)
      sprintf(buf, "a day");
    else if (delta->tm_yday >= 7)
      if ((delta->tm_yday / 7) == 1)
	sprintf(buf, "a week");
      else
	sprintf(buf, "%d weeks", (delta->tm_yday / 7));
    else
      sprintf(buf, "%d days", delta->tm_yday);
  }
  else if (delta->tm_hour > 0)
  {
    if (delta->tm_hour == 1)
      sprintf(buf, "an hour");
    else
      sprintf(buf, "%d hours", delta->tm_hour);
  }
  else if (delta->tm_min > 0)
  {
    if (delta->tm_min == 1)
      sprintf(buf, "a minute");
    else
      sprintf(buf, "%d minutes", delta->tm_min);
  }
  else
  {
    if (delta->tm_sec == 1)
      sprintf(buf, "a second");
    else
      sprintf(buf, "%d seconds", delta->tm_sec);
  }
  return stralloc(buf);
}

char *time_format_4(dt)
time_t dt;
{
  register struct tm *delta;
  char buf[64];
  char times[6][12];
  int i = 0;
  int days, years;

  if (dt < 0)
    dt = 0;

  delta = gmtime(&dt);

  years = delta->tm_year - 70;

  if (years > 0)
  {
    if (years == 1)
      sprintf(times[0], "a year");
    else
      sprintf(times[0], "%d years", delta->tm_year);
    i++;
  }
  else
    *times[0] = '\0';

  if (delta->tm_yday > 7)
  {
    if ((delta->tm_yday / 7) == 1)
      sprintf(times[1], "a week");
    else
      sprintf(times[1], "%d weeks", (delta->tm_yday / 7));
    i++;
    days = delta->tm_yday % 7;
  }
  else
  {
    *times[1] = '\0';
    days = delta->tm_yday;
  }
  if (days > 0)
  {
    if (days == 1)
      sprintf(times[2], "a day");
    else
      sprintf(times[2], "%d days", days);
    i++;
  }
  else
    *times[2] = '\0';
  if (delta->tm_hour > 0)
  {
    if (delta->tm_hour == 1)
      sprintf(times[3], "an hour");
    else
      sprintf(times[3], "%d hours", delta->tm_hour);
    i++;
  }
  else
    *times[3] = '\0';
  if (delta->tm_min > 0)
  {
    if (delta->tm_min == 1)
      sprintf(times[4], "a minute");
    else
      sprintf(times[4], "%d minutes", delta->tm_min);
    i++;
  }
  else
    *times[4] = '\0';
  if (delta->tm_sec > 0)
  {
    if (delta->tm_sec == 1)
      sprintf(times[5], "a second");
    else
      sprintf(times[5], "%d seconds", delta->tm_sec);
    i++;
  }
  else
    *times[5] = '\0';

  switch (i)
  {
  case 0:
    *buf = '\0';
    break;
  case 1:
    {
      int index;

      for (index = 0; index <= 5; index++)
	if (*times[index])
	{
	  sprintf(buf, times[index]);
	  return stralloc(buf);
	}
    }
    break;
  default:
    {
      int index, hits = 0;

      for (index = 0; index <= 5; index++)
      {
	if (*times[index])
	{
	  if (hits++ == 0)
	    strcpy(buf, times[index]);
	  else if (hits == i)
	  {
	    strcat(buf, tprintf(", and %s", times[index]));
	    return stralloc(buf);
	  }
	  else
	  {
	    strcat(buf, tprintf(", %s", times[index]));
	  }
	}
      }
    }
    break;
  }
  return stralloc(buf);
}

char *time_stamp(dt)
time_t dt;
{
  struct tm *delta;
  char buf2[9];

/*
   if(dt < 0) dt = 0;
 */
  delta = localtime(&dt);

  sprintf(buf2, "%02d:%02d:%02d", delta->tm_hour, delta->tm_min, delta->tm_sec);
  return stralloc(buf2);
}

