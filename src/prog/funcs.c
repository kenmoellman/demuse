/* C code produced by gperf version 2.7 */
/* Command-line: gperf -t -G -p -D -r -j 0 -k * -N lookup_funcs_gperf -H hash_funcs_gperf  */
struct fun {char *name; void (*func)(); int nargs;};

#define TOTAL_KEYWORDS 122
#define MIN_WORD_LENGTH 1
#define MAX_WORD_LENGTH 10
#define MIN_HASH_VALUE 14
#define MAX_HASH_VALUE 788
/* maximum key range = 775, duplicates = 0 */

#ifdef __GNUC__
__inline
#endif
static unsigned int
hash_funcs_gperf (str, len)
     register const char *str;
     register unsigned int len;
{
  static unsigned short asso_values[] =
    {
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789,  97, 789,  87,  57,  73,
      103, 100,  29, 116,  21,  17,  85, 115, 112,   8,
       31,  44, 112,  73,  58,  13, 106,  24,  60,  10,
      117,  66,   7, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789, 789, 789, 789, 789,
      789, 789, 789, 789, 789, 789
    };
  register int hval = len;

  switch (hval)
    {
      default:
      case 10:
        hval += asso_values[(unsigned char)str[9]];
      case 9:
        hval += asso_values[(unsigned char)str[8]];
      case 8:
        hval += asso_values[(unsigned char)str[7]];
      case 7:
        hval += asso_values[(unsigned char)str[6]];
      case 6:
        hval += asso_values[(unsigned char)str[5]];
      case 5:
        hval += asso_values[(unsigned char)str[4]];
      case 4:
        hval += asso_values[(unsigned char)str[3]];
      case 3:
        hval += asso_values[(unsigned char)str[2]];
      case 2:
        hval += asso_values[(unsigned char)str[1]];
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval;
}

static struct fun wordlist[] =
  {
    {"s",      fun_s,      1},
    {"if",     fun_if,     2},
    {"v",      fun_v,      1},
    {"sin",    fun_sin,    1},
    {"num",    fun_num,    1},
    {"zwho",   fun_zwho,   1},
    {"sub",    fun_sub,    2},
    {"has",    fun_has,    2},
    {"fsub",   fun_fsub,   2},
    {"tms",    fun_tms,    1},
    {"mid",    fun_mid,    3},
    {"cos",    fun_cos,    1},
    {"ln",     fun_ln,     1},
    {"tmf",	fun_tmf,    1},
    {"mul",    fun_mul,    2},
    {"uinfo",	fun_uinfo,  2},
    {"con",    fun_con,    1},
    {"mod",    fun_mod,    2},
    {"abs",    fun_abs,    1},
    {"bor",    fun_bor,    2},
    {"sgn",    fun_sgn,    1},
    {"pow",    fun_pow,    2},
    {"pos",    fun_pos,    2},
    {"fmul",   fun_fmul,   2},
    {"lnum",   fun_lnum,   1},
    {"div",    fun_div,    2},
    {"zone",   fun_zone,   1},
    {"host",   fun_host,   1},
    {"fabs",   fun_fabs,    1},
    {"lwho",   fun_lwho,   0},
    {"fsgn",   fun_fsgn,    1},
    {"spc",    fun_spc,    1},
    {"onfor",  fun_onfor,  1},
    {"fdiv",   fun_fdiv,   2},
    {"s_as",   fun_s_as,   3},
    {"lor",    fun_lor,    2},
    {"is_a",   fun_is_a,   2},
    {"tan",    fun_tan,    1},
    {"first",  fun_first,  1},
    {"tml",    fun_tml,    1},
    {"name",   fun_name,   1},
    {"loc",    fun_loc,    1},
    {"time",  fun_time, -1},
    {"inzone", fun_inzone, 1},
    {"comp",   fun_comp,   2},
    {"bnot",   fun_bnot,   1},
    {"mtime",  fun_mtime,  -1},
    {"switch", fun_switch,-1},
    {"owner",  fun_owner,  1},
    {"sqrt",   fun_sqrt,   1},
    {"scomp",  fun_scomp,  2},
    {"mstime",   fun_mstime,-1},
    {"tmfl",	fun_tmfl,   1},
    {"base",   fun_base,   3},
    {"s_with", fun_s_with, -1},
    {"fcomp",  fun_fcomp,  2},
    {"flip",   fun_flip,   1},
    {"log",    fun_log,    1},
    {"link",   fun_link,   1},
    {"bxor",   fun_bxor,   2},
    {"rest",   fun_rest,   1},
    {"band",   fun_band,   2},
    {"rand",   fun_rand,   1},
    {"fsqrt",  fun_fsqrt,   1},
    {"arcsin", fun_arcsin, 1},
    {"rjust",  fun_rjust,  2},
    {"wcount", fun_wcount, 1},
    {"add",    fun_add,    2},
    {"lnot",   fun_lnot,   1},
    {"lzone",  fun_lzone,  1},
    {"match",  fun_match,  2},
    {"class",  fun_class,  1},
    {"cname",	fun_cname,  1},
    {"objmem", fun_objmem, 1},
    {"ctime",  fun_ctime,  1},
    {"has_a",  fun_has_a,  2},
    {"wmatch", fun_wmatch, 2},
    {"truth",  fun_truth,  1},
    {"port",   fun_port,   1},
    {"get",    fun_get,   -1},
    {"fadd",   fun_fadd,   2},
    {"exp",    fun_exp,    1},
    {"lxor",   fun_lxor,   2},
    {"idle",   fun_idle,   1},
    {"land",   fun_land,   2},
    {"quota",  fun_quota  ,1},
    {"exit",   fun_exit,   1},
    {"ljust",  fun_ljust,  2},
    {"string", fun_string, 2},
    {"xtime",  fun_xtime, -1},
    {"arccos", fun_arccos, 1},
    {"next",   fun_next,   1},
    {"rmatch", fun_rmatch, 2},
    {"flags",  fun_flags,  1},
    {"ctrunc", fun_ctrunc, 2},
    {"remove", fun_remove, 3},
    {"ifelse", fun_ifelse, 3},
    {"cstrip",	fun_cstrip, 1},
    {"type",   fun_type,   1},
    {"modtime",  fun_modtime,  1},
    {"universe",fun_universe,1},
    {"linkup", fun_linkup, 1},
    {"foreach",fun_foreach,2},
    {"strlen", fun_strlen, 1},
    {"objlist", fun_objlist, 1},
    {"arctan", fun_arctan, 1},
    {"strcat", fun_strcat, 2},
    {"s_as_with",fun_s_as_with,-1},
    {"lattr",  fun_lattr,  1},
    {"credits",fun_credits,1},
    {"controls", fun_controls, 3},
    {"playmem",fun_playmem,1},
    {"getzone",fun_getzone,1},
    {"parents",fun_parents,1},
    {"children",fun_children,1},
    {"entrances", fun_entrances, 1},
    {"delete", fun_delete, 3},
    {"timedate",   fun_timedate,  -1},
    {"attropts",fun_attropts, -1},
    {"extract",fun_extract,3},
    {"lattrdef",fun_lattrdef,1},
    {"quota_left",fun_quota_left,1}
  };

static short lookup[] =
  {
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,   0,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,   2,  -1,  -1,   3,  -1,   4,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,   5,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,   6,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,   7,  -1,  -1,   8,  -1,  -1,
      9,  10,  -1,  11,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  12,  13,  14,  -1,  -1,
     15,  16,  -1,  -1,  -1,  -1,  -1,  -1,  17,  -1,
     18,  -1,  19,  20,  -1,  -1,  -1,  -1,  -1,  21,
     -1,  -1,  22,  -1,  -1,  -1,  -1,  23,  -1,  24,
     -1,  -1,  -1,  25,  -1,  -1,  26,  -1,  27,  -1,
     28,  29,  -1,  30,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  31,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  32,  -1,  33,  34,  -1,  -1,  35,  36,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  37,  38,  39,
     40,  -1,  41,  -1,  -1,  42,  43,  -1,  -1,  -1,
     -1,  44,  45,  -1,  46,  -1,  47,  -1,  48,  -1,
     -1,  -1,  -1,  -1,  49,  50,  -1,  -1,  51,  52,
     -1,  53,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     54,  55,  -1,  -1,  56,  57,  -1,  -1,  -1,  58,
     59,  60,  61,  62,  63,  64,  -1,  -1,  -1,  -1,
     -1,  65,  -1,  -1,  66,  -1,  67,  68,  -1,  69,
     70,  -1,  -1,  71,  72,  -1,  -1,  -1,  73,  74,
     75,  76,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     77,  -1,  -1,  -1,  78,  79,  80,  -1,  -1,  -1,
     -1,  -1,  81,  -1,  -1,  82,  83,  84,  -1,  85,
     -1,  -1,  -1,  -1,  86,  87,  -1,  88,  -1,  -1,
     -1,  -1,  -1,  89,  90,  -1,  -1,  -1,  91,  92,
     -1,  -1,  93,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  94,  -1,  -1,  -1,  -1,  95,  96,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  97,  -1,  -1,  98,  -1,
     -1,  -1,  -1,  99,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1, 100,  -1,  -1,  -1,  -1,  -1, 101,  -1, 102,
     -1,  -1,  -1,  -1,  -1,  -1, 103,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1, 104,  -1,  -1,  -1,  -1,  -1,  -1, 105, 106,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    107,  -1,  -1,  -1, 108,  -1,  -1, 109,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1, 110,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    111,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1, 112,  -1,  -1, 113,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1, 114,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1, 115,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1, 116,  -1,  -1,
     -1,  -1,  -1,  -1,  -1, 117,  -1,  -1,  -1,  -1,
    118,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1, 119,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1, 120,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1, 121
  };

#ifdef __GNUC__
__inline
#endif
extern struct fun *
lookup_funcs_gperf (str, len)
     register const char *str;
     register unsigned int len;
{
  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = hash_funcs_gperf (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register int index = lookup[key];

          if (index >= 0)
            {
              register const char *s = wordlist[index].name;

              if (*str == *s && !strcmp (str + 1, s + 1))
                return &wordlist[index];
            }
          else if (index < -TOTAL_KEYWORDS)
            {
              register int offset = - 1 - TOTAL_KEYWORDS - index;
              register struct fun *wordptr = &wordlist[TOTAL_KEYWORDS + lookup[offset]];
              register struct fun *wordendptr = wordptr + -lookup[offset + 1];

              while (wordptr < wordendptr)
                {
                  register const char *s = wordptr->name;

                  if (*str == *s && !strcmp (str + 1, s + 1))
                    return wordptr;
                  wordptr++;
                }
            }
        }
    }
  return 0;
}
