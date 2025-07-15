#define UNIVERSE(x) (db[get_zone_first(x)].universe)

#define UF_BOOL 0
#define UF_INT 1
#define UF_FLOAT 2
#define UF_STRING 3

struct universe_config {
  char *label;
  int type;
  char *def;
};
extern struct universe_config univ_config[];

#define UA_TELEPORT 0
#define UA_DESTREXIT 1
#define UA_DESTRTHING 2
#define UA_PKILL 3
#define UA_REALECON 4
#define UA_CURRNAME 5
#define UA_EXCHGRATE 6
#define UA_TECHLEVEL 7
#define NUM_UA 8 /* this should always be 1 more than the # of the last UA */

#ifdef __DO_DB_C__
struct universe_config univ_config[] = {
  {"Teleportation",UF_BOOL,"1"},
  {"Destroyable Exits",UF_BOOL,"0"},
  {"Destroyable Things",UF_BOOL,"0"},
  {"Player Killing",UF_BOOL,"0"},
  {"Realistic Economy",UF_BOOL,"0"},
  {"Local Currency Name",UF_STRING,"Dollar"},
  {"Exchange Rate",UF_FLOAT,"1.0"},
  {"Tech Level",UF_INT,"7"},
  {0,0,0}};
#endif /* __DO_DB_C_ */
