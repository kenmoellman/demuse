struct http_struct {
  char authent[21];
  char **text;
  int lines;
  dbref control, user;
};
