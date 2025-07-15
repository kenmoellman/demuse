struct fun {char *name; void (*func)(); int nargs;};
%%
is_a,   fun_is_a,   2
rand,   fun_rand,   1
timedate,   fun_timedate,  -1
ctime,  fun_ctime,  1
mtime,  fun_mtime,  -1
mstime,   fun_mstime,-1
modtime,  fun_modtime,  1
xtime,  fun_xtime, -1
time,  fun_time, -1
class,  fun_class,  1
foreach,fun_foreach,2
get,    fun_get,   -1
has_a,  fun_has_a,  2
has,    fun_has,    2
attropts,fun_attropts, -1
switch, fun_switch,-1
playmem,fun_playmem,1
objmem, fun_objmem, 1
mid,    fun_mid,    3
delete, fun_delete, 3
add,    fun_add,    2
mul,    fun_mul,    2
div,    fun_div,    2
mod,    fun_mod,    2
sub,    fun_sub,    2
rmatch, fun_rmatch, 2
fadd,   fun_fadd,   2
fmul,   fun_fmul,   2
fdiv,   fun_fdiv,   2
fsub,   fun_fsub,   2
band,   fun_band,   2
bor,    fun_bor,    2
bxor,   fun_bxor,   2
bnot,   fun_bnot,   1
land,   fun_land,   2
lor,    fun_lor,    2
lxor,   fun_lxor,   2
lnot,   fun_lnot,   1
truth,  fun_truth,  1
base,   fun_base,   3
parents,fun_parents,1
children,fun_children,1
universe,fun_universe,1
uinfo,	fun_uinfo,  2
sqrt,   fun_sqrt,   1
sgn,    fun_sgn,    1
abs,    fun_abs,    1
fsqrt,  fun_fsqrt,   1
fsgn,   fun_fsgn,    1
fabs,   fun_fabs,    1
first,  fun_first,  1
strcat, fun_strcat, 2
rest,   fun_rest,   1
flags,  fun_flags,  1
strlen, fun_strlen, 1
comp,   fun_comp,   2
fcomp,  fun_fcomp,  2
scomp,  fun_scomp,  2
v,      fun_v,      1
s,      fun_s,      1
s_as,   fun_s_as,   3
s_with, fun_s_with, -1
s_as_with,fun_s_as_with,-1
quota,  fun_quota  ,1
entrances, fun_entrances, 1
quota_left,fun_quota_left,1
credits,fun_credits,1
pos,    fun_pos,    2
match,  fun_match,  2
extract,fun_extract,3
remove, fun_remove, 3
num,    fun_num,    1
con,    fun_con,    1
next,   fun_next,   1
owner,  fun_owner,  1
loc,    fun_loc,    1
link,   fun_link,   1
linkup, fun_linkup, 1
exit,   fun_exit,   1
name,   fun_name,   1
cname,	fun_cname,  1
zone,   fun_zone,   1
getzone,fun_getzone,1
lzone,  fun_lzone,  1
wmatch, fun_wmatch, 2
inzone, fun_inzone, 1
zwho,   fun_zwho,   1
objlist, fun_objlist, 1
controls, fun_controls, 3
sin,    fun_sin,    1
cos,    fun_cos,    1
tan,    fun_tan,    1
arcsin, fun_arcsin, 1
arccos, fun_arccos, 1
arctan, fun_arctan, 1
log,    fun_log,    1
ln,     fun_ln,     1
exp,    fun_exp,    1
pow,    fun_pow,    2
if,     fun_if,     2
ifelse, fun_ifelse, 3
wcount, fun_wcount, 1
lwho,   fun_lwho,   0
spc,    fun_spc,    1
flip,   fun_flip,   1
lnum,   fun_lnum,   1
cstrip,	fun_cstrip, 1
ctrunc, fun_ctrunc, 2
string, fun_string, 2
ljust,  fun_ljust,  2
rjust,  fun_rjust,  2
lattrdef,fun_lattrdef,1
lattr,  fun_lattr,  1
type,   fun_type,   1
idle,   fun_idle,   1
onfor,  fun_onfor,  1
host,   fun_host,   1
port,   fun_port,   1
tms,    fun_tms,    1
tml,    fun_tml,    1
tmf,	fun_tmf,    1
tmfl,	fun_tmfl,   1
%%
