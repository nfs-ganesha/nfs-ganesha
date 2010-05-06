/* NSM Interface */

/*
 * This defines the maximum length of the string
 * identifying the caller.
 */

const SM_MAXSTRLEN = 1024;
const SM_PROG = 100024;
const SM_VERS = 1;
const SM_MON  = 2;
const SM_UNMON = 3;
const SM_UNMON_ALL = 4;

enum res {
  STAT_SUCC = 0,   /*  NSM agrees to monitor.  */
  STAT_FAIL = 1    /*  NSM cannot monitor.  */
};

struct sm_stat_res {
  res    res_stat;
  int    state;

};

struct sm_stat
{
  int state; /* state number of NSM */
};

struct my_id {
  string my_name<SM_MAXSTRLEN>;  /*  hostname  */
  int    my_prog;                /*  RPC program number  */
  int    my_vers;                /*  program version number  */
  int    my_proc;                /*  procedure number  */
};

struct mon_id {
  string mon_name<SM_MAXSTRLEN>; /* name of the host to be monitored */
  struct my_id my_id;
};

struct mon {
  struct mon_id mon_id;
  opaque    priv[16];        /*  private information  */
};

#ifdef RPC_HDR
%extern int nsm_monitor(char *host);
%extern int nsm_unmonitor(char *host);
%extern int nsm_unmonitor_all(void);
#endif
