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

#include <rpcsecgss/rpc/rpcsecgss_rename.h> 
#include <rpcsecgss/rpc/rpc.h>
#include <rpcsecgss/rpc/auth.h>
#include <rpcsecgss/rpc/auth_gss.h>
#include <rpcsecgss/rpc/svc.h>
#include <rpc/clnt.h>
#include <rpc/xdr.h>
#include <rpc/pmap_clnt.h> 

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
char options[] = "hL:s:S:" ;

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

void dispatch( struct svc_req * ptr_req, SVCXPRT * ptr_svc )
{
  int val ;

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
      fprintf( stderr, "gethostname impossible errno=%u|%s\n", errno, strerror( errno ) ) ;
      exit( 1 ) ;
    }
  
  /* Si un enregistrement au portmapper avait eu lieu, je le latte */
  pmap_unset( rpc_service_num, V1 ) ;

  /* Petit coucou initial */
  printf( "Demarrage du serveur toto-server-rpc\n" ) ;
  printf( "Le nom de la machine est %s\n", machine_locale ) ;
  printf( "J'utilise le service RPC %d\n", rpc_service_num ) ;

#ifdef HAVE_KRB5____
     if( ( maj_stat = krb5_gss_register_acceptor_identity( KEYTAB ) ) != GSS_S_COMPLETE )
       {
         char msg[256] ;

         sperror_gss( msg, maj_stat, min_stat ) ;
         printf( "Erreur dans krb5_gss_register_acceptor_identity pour nom %s: %d|%d = %s\n",
                                 gss_service, maj_stat, min_stat, msg ) ;

         exit( 1 ) ;
       }

#endif

  authgss_set_debug_level( 10 ) ; 

  /* On positionne le nom du principal GSSAPI */
  if( !rpcsecgss_svcauth_gss_set_svc_name( gss_service,
                                           mech,
                                           0 /* req_time */,
				           rpc_service_num, V1 ) )
    {
      printf( "svcauth_gss_set_svc_name impossible\n" ) ;
      exit( 1 ) ;
    }

  /* Creation du handler SVC */
  if( ( ptr_svc = rpcsecgss_svctcp_create( RPC_ANYSOCK, SEND_SIZE , RECV_SIZE ) ) == NULL )
    {
      printf( "svctcp_create impossible\n" ) ;
      exit( 1 ) ;
    }

   /* Enregistrement du service */
  fprintf( stderr, "Enregistrement sur le service %d\n", rpc_service_num ) ;
  if( rpcsecgss_svc_register( ptr_svc, rpc_service_num, V1, dispatch, IPPROTO_TCP ) == FALSE )
    {
      printf( "svc_register impossible\n" ) ;
      exit( 1 ) ;
    }
  fprintf( stderr, "------------------------------------------\n" ) ;

  /* Silence, on tourne .... */
  rpcsecgss_svc_run() ;
}
