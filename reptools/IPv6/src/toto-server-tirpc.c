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


#include <rpc/rpc.h>

#define TraduireAdresse( adresse, dest )                   \
           sprintf( dest, "%d.%d.%d.%d",                   \
                  ( ntohl( adresse ) & 0xFF000000 ) >> 24, \
                  ( ntohl( adresse ) & 0x00FF0000 ) >> 16, \
                  ( ntohl( adresse ) & 0x0000FF00 ) >> 8,  \
                  ( ntohl( adresse ) & 0x000000FF ) )
#define TIMEOUT_SEC 25 
#define DEFAULT_RPC_SERVICE 300400 
#define DEFAULT_PORT 8888
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


void dispatch( struct svc_req * ptr_req, SVCXPRT * ptr_svc )
{
  int val ;

  struct svc_rpc_gss_data * gd = NULL ;

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
  unsigned short     port = ntohs( DEFAULT_PORT ) ;
  struct             netconfig * nconf ;
  struct netbuf      netbuf ;
  struct t_bind      bindaddr;
  struct sockaddr_in adresse_rpc ;
  int                sock ;

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
  
  /* Petit coucou initial */
  fprintf( stderr, "Demarrage du serveur toto-server-rpc\n" ) ;
  fprintf( stderr, "Le nom de la machine est %s\n", machine_locale ) ;
  fprintf( stderr, "J'utilise le service RPC %d\n", rpc_service_num ) ;

  adresse_rpc.sin_port        = port  ;
  adresse_rpc.sin_family      = AF_INET ;
  adresse_rpc.sin_addr.s_addr = INADDR_ANY ;

  netbuf.maxlen = sizeof( adresse_rpc );
  netbuf.len    = sizeof( adresse_rpc );
  netbuf.buf    = &adresse_rpc;

  /* bindaddr */
  bindaddr.qlen = SOMAXCONN;
  bindaddr.addr = netbuf;

  /* initialisation des structures de TI-RPC */
  if( ( nconf = (struct netconfig *)getnetconfigent( "tcp" ) ) == NULL )
   {
     fprintf( stderr, "Erreur de getnetconfigent\n" ) ;
     exit( 1 ) ;
   }

  /* Unset sur rpcbind */
  rpcb_unset( rpc_service_num, V1, nconf ) ;

  /* Creation de la socket */
  if( ( sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP) ) < 0 )
    {
      fprintf( stderr, "socket impossible\n" ) ;
      exit( 1 ) ;
    }

  /* Creation du handler SVC */
  if( ( ptr_svc = svc_tli_create( sock, nconf, &bindaddr, SEND_SIZE , RECV_SIZE ) ) == NULL )
    {
      fprintf( stderr, "svctcp_create impossible\n" ) ;
      exit( 1 ) ;
    }

   /* Enregistrement du service */
  fprintf( stderr, "Enregistrement sur le service %d\n", rpc_service_num ) ;
  if( svc_reg( ptr_svc, rpc_service_num, V1, dispatch, nconf ) == FALSE )
    {
      fprintf( stderr, "svc_register impossible\n" ) ;
      exit( 1 ) ;
    }


  fprintf( stderr, "------------------------------------------\n" ) ;

  /* Silence, on tourne .... */
  svc_run() ;

  freenetconfigent(nconf);
  svc_unreg( rpc_service_num, V1);

}
