#include <unistd.h>
#include <stdlib.h> 
#include <string.h> 

#include <sys/errno.h>
#include <sys/param.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>

#include <gssrpc/rpc.h>
#include <gssrpc/clnt.h>
#include <gssrpc/xdr.h>
#include <gssrpc/auth.h>
#include <gssrpc/auth_gss.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h> 


#define TraduireAdresse( adresse, dest )                   \
           sprintf( dest, "%d.%d.%d.%d",                   \
                  ( ntohl( adresse ) & 0xFF000000 ) >> 24, \
                  ( ntohl( adresse ) & 0x00FF0000 ) >> 16, \
                  ( ntohl( adresse ) & 0x0000FF00 ) >> 8,  \
                  ( ntohl( adresse ) & 0x000000FF ) )
#define TIMEOUT_SEC 25 
#define DEFAULT_RPC_SERVICE 300400 
#define SERVICE_NAME "toto@localhost" 
#define KEYTAB "/etc/krb5.keytab"
#define RECV_SIZE 2048
#define SEND_SIZE 2048
#define V1 1 
#define PROC_NULL  0 
#define PROC_PLUS1 1
/* les options pour getopt */
char options[] = "hL:N:s:S:" ;

/* L'aide en ligne */
char utilisation[] = 
"Utilisation: %s [-hLsS] \n"
"\t[-h]                   affiche cet aide en ligne\n"
"\t[-L <logfile>]         indique le fichier de log\n"
"\t[-N <NivDebug>]        indique le niveau de debug pour les journaux\n" 
"\t[-s <service RPC>]     indique le port ou le service a utiliser\n" 
"\t[-S <service GSSAPI>]  indique le service pour la GSSAPI\n" ;


/* Des variables globales (en general ecrites une seule fois, relues plusieurs) */
static char logfile_name[MAXPATHLEN] ;    /* Le chemin du fichier de log           */
unsigned int rpc_service_num = DEFAULT_RPC_SERVICE ;

#define SVCAUTH_PRIVATE(auth) \
        (*(struct svc_rpc_gss_data **)&(auth)->svc_ah_private)




struct svc_rpc_gss_data {
      bool_t                  established;    /* context established */
      gss_ctx_id_t            ctx;            /* context id */
      struct rpc_gss_sec      sec;            /* security triple */
      gss_buffer_desc         cname;          /* GSS client name */
      u_int                   seq;            /* sequence number */
      u_int                   win;            /* sequence window */
      u_int                   seqlast;        /* last sequence number */
      uint32_t                seqmask;        /* bitmask of seqnums */
      gss_name_t              client_name;    /* unparsed name string */
      gss_buffer_desc         checksum;       /* so we can free it */
    };


void log_badauth_display_status_1(OM_uint32 code, int type, int rec)
{
     OM_uint32 gssstat, minor_stat, msg_ctx;
     gss_buffer_desc msg;

     msg_ctx = 0;
     while (1) {
          gssstat = gss_display_status(&minor_stat, code,
                                       type, GSS_C_NULL_OID,
                                       &msg_ctx, &msg);
          if (gssstat != GSS_S_COMPLETE) {
               if (!rec) {
                    log_badauth_display_status_1(gssstat,GSS_C_GSS_CODE,1);
                    log_badauth_display_status_1(minor_stat,
                                                 GSS_C_MECH_CODE, 1);
               } else
                    printf("GSS-API authentication error %.*s: "
                           "recursive failure!\n", (int) msg.length,
                           (char *)msg.value);
               return;
          }

          printf(", %.*s", (int) msg.length, (char *)msg.value);
          (void) gss_release_buffer(&minor_stat, &msg);

          if (!msg_ctx)
               break;
     }
}

void log_badauth_display_status(OM_uint32 major, OM_uint32 minor)
{
     log_badauth_display_status_1(major, GSS_C_GSS_CODE, 0);
     log_badauth_display_status_1(minor, GSS_C_MECH_CODE, 0);
}

static void rpc_test_badverf(gss_name_t client, gss_name_t server,
                             struct svc_req *rqst, struct rpc_msg *msg,
                             caddr_t data)
{
     OM_uint32 minor_stat;
     gss_OID type;
     gss_buffer_desc client_name, server_name;

     (void) gss_display_name(&minor_stat, client, &client_name, &type);
     (void) gss_display_name(&minor_stat, server, &server_name, &type);

     printf("rpc_test server: bad verifier from %.*s at %s:%d for %.*s\n",
            (int) client_name.length, (char *) client_name.value,
            inet_ntoa(rqst->rq_xprt->xp_raddr.sin_addr),
            ntohs(rqst->rq_xprt->xp_raddr.sin_port),
            (int) server_name.length, (char *) server_name.value);

     (void) gss_release_buffer(&minor_stat, &client_name);
     (void) gss_release_buffer(&minor_stat, &server_name);
}



void rpc_test_badauth(OM_uint32 major, OM_uint32 minor,
                 struct sockaddr_in *addr, caddr_t data)
{
     char *a;

     /* Authentication attempt failed: <IP address>, <GSS-API error */
     /* strings> */

     printf("rpc_test server: Authentication attempt failed: %s", a);
     log_badauth_display_status(major, minor);
     printf("\n");
}

void log_miscerr(struct svc_req *rqst, struct rpc_msg *msg,
                 char *error, char *data)
{
     char *a;

     printf("Miscellaneous RPC error: %s\n", error);
}

void dispatch( struct svc_req * ptr_req, SVCXPRT * ptr_svc )
{
  int val ;

  struct svc_rpc_gss_data * gd = NULL ;
  gss_buffer_desc           oidbuff ;
  gss_name_t                src_name, targ_name;
  OM_uint32                 maj_stat = 0 ;
  OM_uint32                 min_stat = 0 ;

  switch( ptr_req->rq_proc )
    {
    case PROC_NULL:
       fprintf( stderr, "Appel a PROC_NULL\n" ) ;

      if( svc_getargs( ptr_svc, (xdrproc_t)xdr_void, NULL ) == FALSE )
        svcerr_decode( ptr_svc ) ;
      if( svc_sendreply( ptr_svc, (xdrproc_t)xdr_void, NULL ) == FALSE )
        svcerr_decode( ptr_svc ) ;
      break ;
      
    case PROC_PLUS1:
      fprintf( stderr, "Appel a PROC_PLUS1\n" ) ;
     
      if( ptr_req->rq_cred.oa_flavor == RPCSEC_GSS ) 
       {

           fprintf( stderr, "Utilisation de RPCSEC_GSS\n" ) ;
          /* Extract the credentials */
          gd = SVCAUTH_PRIVATE(ptr_req->rq_xprt->xp_auth);
       
          printf( "----> RPCSEC_GSS svc=%u RPCSEC_GSS_SVC_NONE=%u RPCSEC_GSS_SVC_INTEGRITY=%u RPCSEC_GSS_SVC_PRIVACY=%u\n", 
                  gd->sec.svc, RPCSEC_GSS_SVC_NONE, RPCSEC_GSS_SVC_INTEGRITY, RPCSEC_GSS_SVC_PRIVACY ) ;
          printf( "----> Client = %s length=%u   Qop=%u\n", 
	          gd->cname.value, gd->cname.length, gd->sec.qop ) ;

          if( ( maj_stat = gss_oid_to_str( &min_stat, 
					   gd->sec.mech,  
					   &oidbuff ) ) != GSS_S_COMPLETE )
            {
		fprintf( stderr, "Erreur de gss_oid_to_str: %u|%u\n" ) ;
		exit( 1 ) ; 
            }
          printf( "----> Client mech=%s len=%u\n", oidbuff.value, oidbuff.length ) ;

          /* Je fais le menage derriere moi */
          (void)gss_release_buffer( &min_stat, &oidbuff ) ;
       } 

      if( svc_getargs( ptr_svc, (xdrproc_t)xdr_int, (char *)&val ) == FALSE )
        svcerr_decode( ptr_svc ) ;
      
      val = val + 1 ;  /* Ce que fait cette fonction est spectaculaire */
      
      if( svc_sendreply( ptr_svc, (xdrproc_t)xdr_int, (char *)&val ) == FALSE )
        svcerr_decode( ptr_svc ) ;
      
      break ;
    }
  
} /* dispatch */

main( int argc, char * argv[] )
{
  char               nom_exec[MAXPATHLEN] ;
  char *             tempo_nom_exec  = NULL ;
  struct rpcent *    etc_rpc ;               /* pour consulter /etc/rpc ou rpc.bynumber */
  int                c ;                     /* pour getopt                  */
  static char        machine_locale[256] ;
  SVCXPRT *          ptr_svc ;
  char               gss_service[1024] ;
  gss_name_t         gss_service_name ;
  gss_buffer_desc    gss_service_buf ;
  char               mech[] = "kerberos_v5" ;
  OM_uint32          maj_stat, min_stat;


   /* Initialisation des valeurs par defaut */
  strcpy( gss_service,  SERVICE_NAME ) ;


  /* On recupere le nom de l'executable */
  if( ( tempo_nom_exec = strrchr( argv[0], '/' ) ) != NULL )
    strcpy( (char *)nom_exec, tempo_nom_exec + 1 ) ;
  
  while( ( c = getopt( argc, argv, options ) ) != EOF )
    {
      switch( c ) 
        {
        case 'L':
          /* On indique un fichier de log alternatif */
          strcpy( logfile_name, optarg ) ;
          break ;

        case 'S':
          /* Le nom de service */
          strcpy( gss_service, optarg ) ;
          break ;
 
        case 's':
          /* Un nom ou un numero de service a ete indique */
          if( isalpha( (int)*optarg ) )
            {
              /* Ca commence pas par un chiffre donc c'est un nom service */
              if( ( etc_rpc = getrpcbyname( optarg ) ) == NULL )
                {
                  fprintf( stderr, "Impossible de resoudre le service %s\n", optarg ) ;
                }
              else
                {
                  rpc_service_num = etc_rpc->r_number ;
                }
            }
          else
            {
              /* C'est un numero de service qui est indique */
              rpc_service_num = atoi( optarg ) ;
            }
          break ;

        case '?':
        case 'h':
        default:
          /* Cette option affiche l'aide en ligne */
          printf( utilisation, argv[0] ) ;
          exit( 0 ) ;
          break ;
        }
    }

  if( argc != optind )
    {
      fprintf( stderr, utilisation, nom_exec ) ;
      fprintf( stderr, "Pas d'argument additionnel\n" ) ;
      exit( 1 ) ;
    }
  
  /* Obtention du nom de la machine */
  if( gethostname( machine_locale, sizeof( machine_locale ) ) != 0 )
    {
      fprintf( stderr, "error gethostname: errno=%u|%s\n", errno, strerror( errno ) ) ;
      exit( 1 ) ;
    }
  
  /* Si un enregistrement au portmapper avait eu lieu, je le latte */
  pmap_unset( rpc_service_num, V1 ) ;

  /* Petit coucou initial */
  fprintf( stderr, "Demarrage du serveur toto-server-rpc\n" ) ;
  fprintf( stderr, "Le nom de la machine est %s\n", machine_locale ) ;
  fprintf( stderr, "J'utilise le service RPC %d\n", rpc_service_num ) ;

#ifdef HAVE_KRB5
     if( ( maj_stat = krb5_gss_register_acceptor_identity( KEYTAB ) ) != GSS_S_COMPLETE )
       {
         char msg[256] ;

         sperror_gss( msg, maj_stat, min_stat ) ;
         fprintf( stderr, "Erreur dans krb5_gss_register_acceptor_identity pour nom %s: %d|%d = %s\n",
                                 gss_service, maj_stat, min_stat, msg ) ;

         exit( 1 ) ;
       }
#endif
   gss_service_buf.value  = gss_service ;
   gss_service_buf.length = strlen(gss_service_buf.value) + 1; /* ATtention au +1 */


   if( ( maj_stat = gss_import_name( &min_stat, &gss_service_buf,
                                     (gss_OID)GSS_C_NT_HOSTBASED_SERVICE,
                                     &gss_service_name ) ) != GSS_S_COMPLETE )
       {
         char msg[256] ;

         sperror_gss( msg, maj_stat, min_stat ) ;
         fprintf( stderr, "Importation par la GSS-API du nom %s impossible: %d|%d = %s\n",
                                gss_service, maj_stat, min_stat, msg ) ;
         exit( 1 ) ;
       }
     else
       fprintf( stderr, "Nom de service '%s' correctement importe\n", gss_service ) ;
 
  /* On positionne le nom du principal GSSAPI */
  if( !svcauth_gss_set_svc_name( gss_service_name ) )
    {
      fprintf( stderr, "svcauth_gss_set_svc_name impossible\n" ) ;
      exit( 1 ) ;
    }

  /* svcauth_gssapi_set_log_badauth_func(rpc_test_badauth, NULL); */
  /* svcauth_gssapi_set_log_badverf_func(rpc_test_badverf, NULL); */
  /* svcauth_gssapi_set_log_miscerr_func(log_miscerr, NULL);      */

  /* Creation du handler SVC */
  if( ( ptr_svc = svctcp_create( RPC_ANYSOCK, SEND_SIZE , RECV_SIZE ) ) == NULL )
    {
      fprintf( stderr, "svctcp_create impossible\n" ) ;
      exit( 1 ) ;
    }

   /* Enregistrement du service */
  fprintf( stderr, "Enregistrement sur le service %d\n", rpc_service_num ) ;
  if( svc_register( ptr_svc, rpc_service_num, V1, dispatch, IPPROTO_TCP ) == FALSE )
    {
      fprintf( stderr, "svc_register impossible\n" ) ;
      exit( 1 ) ;
    }
  fprintf( stderr, "------------------------------------------\n" ) ;

  /* Silence, on tourne .... */
  svc_run() ;
}
