// File : NFS.i
%module NFS
%{
#include "nfs_proto_functions.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "cmd_nfstools.h"
%}

/* typemaps for setattr/getattr */
%typemap(in) mode3 {
  $1 = (mode3)SvIV($input);
}

%typemap(in) bool_t {
  $1 = (bool_t)SvIV($input);
}

%typemap(in) time_how {
  $1 = (time_how)SvIV($input);
}

/* typemaps for read/write */
%typemap(in) count3 {
  $1 = (count3)SvIV($input);
}

%typemap(in) u_int {
  $1 = (u_int)SvIV($input);
}

%include "nfs_proto_functions.h"
%include "nfs23.h"
%include "nfs4.h"
%include "mount.h"
%include "BuddyMalloc.h"
%include "Svc.h"

%inline %{

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/* Connectathon */
#define	CHMOD_RW	0666
#define	CHMOD_RWX	0777
#define CHMOD_NONE 0

/* global */
time_t ServerBootTime = 0;
verifier4 NFS4_write_verifier ;
writeverf3 NFS3_write_verifier ;

extern int fsal_init(char * filename,
                     int flag_v,
                     FILE * output
           );

extern int cacheinode_init(char * filename,
                           int flag_v,
                           FILE * output
           );

extern int nfs_init(char * filename,
                    int flag_v,
                    FILE * output
           );

int aux_init()
{
  ServerBootTime = time(NULL);

  memset(NFS3_write_verifier, 0, sizeof(writeverf3));
  memcpy(NFS3_write_verifier, &ServerBootTime, sizeof(time_t));
        
  memset(NFS4_write_verifier, 0, sizeof(verifier4));
  memcpy(NFS4_write_verifier, &ServerBootTime, sizeof(time_t));

  return 0;
}

/* @see commands_NFS.c (shell) */
typedef struct shell_fh3__
{
  u_int data_len;
  char  data_val[NFS3_FHSIZE];
} shell_fh3_t;

typedef struct cmdnfs_thr_info__
{
  int is_thread_init;
  fsal_export_context_t exp_context;
  fsal_op_context_t context ;
  struct Authunix_parms authunix_struct;
  cache_inode_client_t client ;
  cache_content_client_t dc_client ;
  int is_mounted_path;
  shell_fh3_t mounted_path_hdl;
  char mounted_path[NFS2_MAXPATHLEN];
  shell_fh3_t current_path_hdl;
  char current_path[NFS2_MAXPATHLEN];
} cmdnfs_thr_info_t;

/* parameters to call NFS functions */
extern exportlist_t  * pexportlist;

extern cmdnfs_thr_info_t * GetNFSClient();

extern int InitNFSClient( cmdnfs_thr_info_t * p_thr_info );

caddr_t get_ClntCred(cmdnfs_thr_info_t * pthr)
{
  return (caddr_t) &(pthr->authunix_struct);
}

fsal_op_context_t * get_Context(cmdnfs_thr_info_t * pthr)
{
  return &(pthr->context);
}

cache_inode_client_t * get_Client(cmdnfs_thr_info_t * pthr)
{
  return &(pthr->client);
}

extern hash_table_t * ht ;

u_long get_Prog(int prog)
{
  return (u_long)prog;
}

u_long get_Vers(int vers)
{
  return (u_long)vers;
}

/* Mount 3 */

/* to get a fh */
int get_new_nfs_fh3( char * path, nfs_fh3 * fh)
{
  char str_path[NFS2_MAXPATHLEN];
  char * pstr_path = str_path;
  int rc;

  strncpy(str_path, path ,NFS2_MAXPATHLEN);

  if (str_path[0] == '@') {
    rc = cmdnfs_fhandle3(
        CMDNFS_ENCODE, 1, &pstr_path,
        0,   NULL,
        (caddr_t)fh );
    if ( rc != TRUE )
    {
      printf("Invalid FileHandle: %s\n",str_path );
      return -1;
    }
    else return 0;
  }
  return -1;
}

/* mount tools */
nfs_fh3 * get_nfs_fh3_from_fhandle3(fhandle3 *fh) {
  return (nfs_fh3*)fh;
}

void copy_nfs_fh3(nfs_fh3 *fh, nfs_fh3 *f) {
  *f = *fh;
}

int are_nfs_fh3_equal(nfs_fh3 * fh1, nfs_fh3 * fh2)
{
  return (fh1->data.data_len == fh2->data.data_len && !memcmp(fh1->data.data_val, fh2->data.data_val, fh1->data.data_len)) ? TRUE : FALSE;
}

int copy_mountbody_from_res(nfs_res_t * res, mountbody * m) 
{
  if (res->res_dump) {
    memcpy(m, res->res_dump, sizeof(mountbody));
    return 1;
  } else {
    return 0;
  }
}

int copy_exportnode_from_res(nfs_res_t * res, exportnode * m) 
{
  if (res->res_mntexport) {
    memcpy(m, res->res_mntexport, sizeof(exportnode));
    return 1;
  } else {
    return 0;
  }
}

/* NFS 3 */
/* lookup3 */
void copy_nfs_fh3_from_lookup3res(nfs_res_t * res, nfs_fh3 * fh)
{
  fh->data.data_len = res->res_lookup3.LOOKUP3res_u.resok.object.data.data_len;
  fh->data.data_val = res->res_lookup3.LOOKUP3res_u.resok.object.data.data_val;
}

/* getattr3 */
void copy_fattr3_from_res(nfs_res_t * res, fattr3 * fa)
{
  *fa = res->res_getattr3.GETATTR3res_u.resok.obj_attributes;
}

extern void print_nfs_attributes(fattr3 * attrs, FILE * output);

int get_int_from_mode3(mode3 * m)
{
  return (int)*m;
}

int are_mode3_equal(mode3 * m1, int m2)
{
  return (*m1 == (unsigned int)m2) ? TRUE : FALSE;
}

int get_int_from_size3(size3 * m)
{
  return (int)*m;
}

int are_size3_equal(size3 * s1, int s2)
{
  return (*s1 == (unsigned int)s2) ? TRUE : FALSE;
}

int get_int_from_nlink(nfs3_uint32 * n)
{
  return (int)*n;
}

int are_nlink_equal(nfs3_uint32 * n1, int n2)
{
  return (*n1 == (unsigned int)n2) ? TRUE : FALSE;
}

int is_symlink(fattr3 * fa)
{
  return (fa->type == NF3LNK) ? TRUE : FALSE;
}

/* access3 */
void copy_access3_from_res(nfs_res_t * res, nfs3_uint32 * a)
{
  *a = res->res_access3.ACCESS3res_u.resok.access;
}

/* readlink3 */
void copy_READLINK3resok_from_res(nfs_res_t * res, READLINK3resok * r)
{
  *r = res->res_readlink3.READLINK3res_u.resok;
}

/* read3 */
offset3 * get_new_offset3(int i)
{
  offset3 * o = (offset3*)malloc(sizeof(offset3));
  *o = i;
  return o;
}

void free_offset3(offset3 * o)
{
  free(o);
}

void copy_READ3resok_from_res(nfs_res_t * res, READ3resok * r)
{
  *r = res->res_read3.READ3res_u.resok;
}

int is_eof_read3(READ3resok * r)
{
  // BUGAZOMEU
  return (r->eof == TRUE || r->count == 0) ? TRUE: FALSE;
}

char * get_print_data_from_READ3resok(READ3resok * r)
{
  char * s = (char*)malloc(r->data.data_len*sizeof(char));
  sprintf(s, "%s", r->data.data_val);
  return s;
}

void fill_offset3_from_READ3resok(offset3 * o, READ3resok * r)
{
  *o = *o + (offset3)r->count;
}

/* write3 */
void copy_WRITE3resok_from_res(nfs_res_t * res, WRITE3resok * r)
{
  *r = res->res_write3.WRITE3res_u.resok;
}

int get_int_from_count3(count3 * c)
{
  return (int)*c;
}

void fill_offset3_from_WRITE3resok(offset3 * o, WRITE3resok * w)
{
  *o = *o + (offset3)w->count;
}

/* create3 */
void copy_nfs_fh3_from_create3res(nfs_res_t * res, nfs_fh3 * fh)
{
  fh->data.data_len = res->res_create3.CREATE3res_u.resok.obj.post_op_fh3_u.handle.data.data_len;
  fh->data.data_val = res->res_create3.CREATE3res_u.resok.obj.post_op_fh3_u.handle.data.data_val;
}

/* mkdir3 */
void copy_nfs_fh3_from_mkdir3res(nfs_res_t * res, nfs_fh3 * fh)
{
  fh->data.data_len = res->res_mkdir3.MKDIR3res_u.resok.obj.post_op_fh3_u.handle.data.data_len;
  fh->data.data_val = res->res_mkdir3.MKDIR3res_u.resok.obj.post_op_fh3_u.handle.data.data_val;
}

/* symlink3 */
void copy_nfs_fh3_from_symlink3res(nfs_res_t * res, nfs_fh3 * fh)
{
  fh->data.data_len = res->res_symlink3.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle.data.data_len;
  fh->data.data_val = res->res_symlink3.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle.data.data_val;
}

/* readdir3 & readdirplus3 */
cookie3 * get_new_cookie3()
{
  cookie3 * c = (cookie3*)malloc(sizeof(cookie3));
  *c = 0LL;
  return c;
}

void free_cookie3(cookie3 * c)
{
  free(c);
}

void copy_cookieverf_dirlist_from_res(nfs_res_t * res, char * c, dirlist3 * d)
{
  memcpy( c, res->res_readdir3.READDIR3res_u.resok.cookieverf,
      sizeof( cookieverf3 ) );
  *d = res->res_readdir3.READDIR3res_u.resok.reply;
}

void copy_cookieverf_dirlistplus_from_res(nfs_res_t * res, char * c, dirlistplus3 * d)
{
  memcpy( c, res->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
      sizeof( cookieverf3 ) );
  *d = res->res_readdirplus3.READDIRPLUS3res_u.resok.reply;
}

int is_eofplus(dirlistplus3 * d)
{
  return (d->eof == TRUE) ? TRUE: FALSE;
}
int is_eof(dirlist3 * d)
{
  return (d->eof == TRUE) ? TRUE: FALSE;
}

/* fsstat3 */
void copy_FSSTAT3resok_from_res(nfs_res_t * res, FSSTAT3resok * f)
{
  *f = res->res_fsstat3.FSSTAT3res_u.resok;
}

void print_nfs_fsstat(FSSTAT3resok * fs)
{
  printf("\ttbytes : %llu\n", fs->tbytes);
  printf("\tfbytes : %llu\n", fs->fbytes);
  printf("\tabytes : %llu\n", fs->abytes);
  printf("\ttfiles : %llu\n", fs->tfiles);
  printf("\tffiles : %llu\n", fs->ffiles);
  printf("\tafiles : %llu\n", fs->afiles);
  printf("\tinvarsec : %#o\n",fs->invarsec);
}

/* fsinfo */
void copy_FSINFO3resok_from_res(nfs_res_t * res, FSINFO3resok * f)
{
  *f = res->res_fsinfo3.FSINFO3res_u.resok;
}

void print_nfs_fsinfo(FSINFO3resok * fi)
{
  printf("\trtmax : %#o\n",fi->rtmax);
  printf("\trtpref : %#o\n",fi->rtpref);
  printf("\trtmult : %#o\n",fi->rtmult);
  printf("\twtmax : %#o\n",fi->wtmax);
  printf("\twtpref : %#o\n",fi->wtpref);
  printf("\twtmult : %#o\n",fi->wtmult);
  printf("\tdtpref : %#o\n",fi->dtpref);
  printf("\tmaxfilesize : %llu\n", fi->maxfilesize);
  printf("\ttime_delta : %s",ctime((time_t*)&fi->time_delta.seconds));
  printf("\tproperties : %#o\n",fi->properties);
}

int get_rtmax_from_FSINFO3resok(FSINFO3resok * fs)
{
  return (int) fs->rtmax;
}

int get_wtmax_from_FSINFO3resok(FSINFO3resok * fs)
{
  return (int) fs->wtmax;
}

/* pathconf3 */
void copy_PATHCONF3resok_from_res(nfs_res_t * res, PATHCONF3resok * p)
{
  *p = res->res_pathconf3.PATHCONF3res_u.resok;
}

void print_nfs_pathconf(PATHCONF3resok * p)
{
  printf("\tlinkmax : %#o\n",p->linkmax);
  printf("\tname_max : %#o\n",p->name_max);
  printf("\tno_trunc : %s\n", p->no_trunc ? "FALSE" : "TRUE");
  printf("\tchown_restricted : %s\n", !p->chown_restricted ? "FALSE" : "TRUE");
  printf("\tcase_insensitive : %s\n", !p->case_insensitive ? "FALSE" : "TRUE");
  printf("\tcase_preserving : %s\n", !p->case_preserving ? "FALSE" : "TRUE");
}

/* print tools */
void print_nfs_res( nfs_res_t * p_res )
{
  unsigned int index;
  for ( index = 0; index < sizeof(nfs_res_t); index++ )
  {
    if ( (index+1)%32 == 0 )
      printf("%02X\n",((char*)p_res)[index]);
    else
      printf("%02X.",((char*)p_res)[index]);
  }
  printf("\n");
}

void print_nfs_arg( nfs_arg_t * p_arg )
{
  unsigned int index;
  for ( index = 0; index < sizeof(nfs_arg_t); index++ )
  {
    if ( (index+1)%32 == 0 )
      printf("%02X\n",((char*)p_arg)[index]);
    else
      printf("%02X.",((char*)p_arg)[index]);
  }
  printf("\n");
}

void print_nfs_fh3(nfs_fh3 *f)
{
  unsigned int index;
  for ( index = 0; index < sizeof(nfs_fh3); index++ )
  {
    if ( (index+1)%32 == 0 )
      printf("%02X\n",((char*)f)[index]);
    else
      printf("%02X.",((char*)f)[index]);
  }
  printf("\n");
}

void print_friendly_nfs_fh3(char * str_name, nfs_fh3 *f) 
{
  char buff[2*NFS3_FHSIZE+1];
  snprintmem( buff, 2*NFS3_FHSIZE+1, (caddr_t)f->data.data_val ,
                          f->data.data_len ) ;
  printf("\t%s (@%s)\n", str_name, buff ) ;
}

FILE * get_output()
{
  return stdout;
}
%}
