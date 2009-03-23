#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/param.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>


#include <rpc/rpc.h>



#include <pthread.h>

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
#define RECV_SIZE 2048
#define SEND_SIZE 2048
#define V1 1 
#define PROC_NULL  0 
#define PROC_PLUS1 1

/* L'aide en ligne */
char options[] = "hd:s:S:v:p:" ;
char utilisation[] =
"Utilisation: %s [-hds] message\n"
"\t[-h]                   affiche cet aide en ligbe\n"
"\t[-d <machine>]         indique la machine serveur\n"
"\t[-s <service RPC>]     indique le port ou le service a utiliser\n" 
"\t[-v <version RPC>]     indique la version du protocole a utiliser\n"
"\t[-p <rpc proc>]        indique le numero de function a utiliser\n"
"\t[-S <service GSSAPI>]  indique service GSSAPI a utiliser\n"  ;


CLIENT * Creer_RPCClient( unsigned int adresse, unsigned int programme, unsigned int version, 
                          unsigned short port, int sockfd )
{
  struct sockaddr_in adresse_rpc ;
  int                sock = 0 ;
  CLIENT *           client ;
  struct timeval     intervalle ;
  int                rc ;
  struct             netconfig * nconf ;
  struct netbuf      netbuf ;
 
  memset( &adresse_rpc, 0, (size_t)sizeof( adresse_rpc ) ) ;
  adresse_rpc.sin_port        = port ;
  adresse_rpc.sin_family      = AF_INET ;
  adresse_rpc.sin_addr.s_addr = adresse ;

  sock               = sockfd ;
  intervalle.tv_sec  = TIMEOUT_SEC ;
  intervalle.tv_usec = 0 ;
  
  if( sock > 0 )
    {
      if( port > 0 )
        {
          /* En tcp, il faut que la socket soit connectee sur le service en face si on n'utilise pas RPC_ANYSOCK 
           * ATTENTION, ceci est une feature non documentee des RPC clientes (j'ai vu ca dans les sources) */
          if( connect( sock, (struct sockaddr *)&adresse_rpc, sizeof( adresse_rpc ) ) < 0 )
            fprintf( stderr, "connect impossible sur le serveur RPC\n" ) ;
        }
      else
        {
          /* Dans ce cas, on ne connait pas le port en face, donc connect impossible, on prend RPC_ANYSOCK 
           * mais uniquement apres avoir ferme la socket 'sock' qui ne sert a rien ici */
          close( sock ) ;
          sock = RPC_ANYSOCK ;
        }
    }

  /* initialisation des structures de TI-RPC */
  if( ( nconf = (struct netconfig *)getnetconfigent( "tcp" ) ) == NULL )
   {
     fprintf( stderr, "Erreur de getnetconfigent\n" ) ;
     exit( 1 ) ;
   }

  netbuf.maxlen = sizeof( adresse_rpc );
  netbuf.len    = sizeof( adresse_rpc );
  netbuf.buf    = &adresse_rpc;

  /* Creation et allocation du client */
  if( ( client = clnt_tli_create( sock, 
                                  nconf, 
                                  &netbuf, 
                                  programme,  
                                  version, 
                                  SEND_SIZE,  
                                  RECV_SIZE ) ) == NULL )
   {
      char erreur[100] ;
      char entete[100] ;
      
      sprintf( entete, "Creation RPC %d|%d|0x%x:%d|%d", programme, version, adresse, port, sock ) ;
      strcpy( erreur, clnt_spcreateerror( entete ) ) ;
          fprintf( stderr, "%s", erreur ) ;
          
          return NULL ;
    }
  
  return client ;
} /* Creer_RPCClient */


main( int argc, char * argv[] ) 
{
  struct timeval   intervalle = { TIMEOUT_SEC, 0 };
  CLIENT *         client ;
  int              c ;  
  struct rpcent *  etc_rpc ;               /* pour consulter /etc/rpc ou rpc.bynumber */
  unsigned int     adresse_serveur ;  /* Au format NET */
  struct hostent * hp ;
  char             nom_exec[MAXPATHLEN] ;
  char             machine_locale[256] ;
  char *           tempo_nom_exec  = NULL ;
  unsigned int     rpc_service_num = DEFAULT_RPC_SERVICE ;
  unsigned int     rpc_version = V1 ;
  unsigned int     rpcproc = PROC_PLUS1 ;
  unsigned short   port =  ntohs( DEFAULT_PORT ) ;
  int              val = 2 ;
  int              rc ;
  char             gss_service[1024] ;


  
   /* On recupere le nom de l'executable */
  if( ( tempo_nom_exec = strrchr( argv[0], '/' ) ) != NULL )
    strcpy( (char *)nom_exec, tempo_nom_exec + 1 ) ;
  
  while( ( c = getopt( argc, argv, options ) ) != EOF )
    {
      switch( c )
        {
        case 'd':
          /* Cette option permet de recuperer un nom pour la machine distante */
          if( isalpha( *optarg ) )
            {
              /* Recuperation de l'adresse de la machine serveur */
              if( ( hp = gethostbyname( optarg ) ) == NULL )
                {
                  fprintf( stderr, "error gethostbyname errono=%u|%s\n", errno, strerror( errno ) ) ;
                  exit( 1 ) ; 
                }
              
              memcpy( &adresse_serveur, hp->h_addr, hp->h_length ) ;
            }
          else
            {
              adresse_serveur = inet_addr( optarg ) ;
            }
          break;

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

        case 'v':
           /* numero de version */
           rpc_version = atoi( optarg ) ;
           break ;

        case 'p':
           rpcproc = atoi( optarg ) ;
           break ;

        case 'h':
        case '?':
        default:
          /* Affichage de l'aide en ligne */
          fprintf( stderr, utilisation, nom_exec ) ;
          exit( 0 ) ;
          break ;
        }
    }
  
  if( ( client = Creer_RPCClient( adresse_serveur, rpc_service_num, rpc_version, port , RPC_ANYSOCK ) ) == NULL )
    {
      char erreur[100] ;
      strcpy( erreur, clnt_spcreateerror( "Creation RPC" ) ) ;
      fprintf( stderr, "Creation RPC: %s\n", erreur ) ;
      exit( 1 ) ;
    }

  client->cl_auth = authunix_create_default();

  val = 2 ;
  fprintf( stderr, "J'envoie la valeur %d\n", val ) ;
  if( ( rc = clnt_call( client, rpcproc, 
                        (xdrproc_t)xdr_int, (caddr_t)&val, 
                        (xdrproc_t)xdr_int, (caddr_t)&val, 
                        intervalle ) ) != RPC_SUCCESS )
    {
      clnt_perror( client, "appel a  FIXE_RET\n" ) ;
      exit ( 1 ) ;
    }
  fprintf( stderr, "Je recois la valeur %d\n", val ) ;

  auth_destroy( client->cl_auth ) ;  
  clnt_destroy( client ) ;
}

