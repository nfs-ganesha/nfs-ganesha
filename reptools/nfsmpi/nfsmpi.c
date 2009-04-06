#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/param.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <rpc/rpc.h>

#include "nfs23.h"
#include "mount.h"

#include <pthread.h>
#include <string.h>
#include <errno.h>

#include <mpi.h>

#define TraduireAdresse( adresse, dest )                   \
           sprintf( dest, "%d.%d.%d.%d",                   \
                  ( ntohl( adresse ) & 0xFF000000 ) >> 24, \
                  ( ntohl( adresse ) & 0x00FF0000 ) >> 16, \
                  ( ntohl( adresse ) & 0x0000FF00 ) >> 8,  \
                  ( ntohl( adresse ) & 0x000000FF ) )
#define TIMEOUT_SEC 25 
#define DEFAULT_RPC_SERVICE 100005 
#define SERVICE_NAME "toto@localhost" 
#define RECV_SIZE 2048
#define SEND_SIZE 2048
#define V3 3 
#define PROC_NULL  0 
#define PROC_PLUS1 1

/* L'aide en ligne */
char options[] = "hd:s:S:v:" ;
char utilisation[] =
"Utilisation: %s [-hds] message\n"
"\t[-h]                   affiche cet aide en ligbe\n"
"\t[-d <machine>]         indique la machine serveur\n"
"\t[-s <service RPC>]     indique le port ou le service a utiliser\n" 
"\t[-v <version RPC>]     indique la version du protocole a utiliser\n"
"\t[-p <rpc proc>]        indique le numero de function a utiliser\n" ;


CLIENT * Creer_RPCClient( unsigned int adresse, unsigned int programme, unsigned int version, 
                          unsigned short port, int sockfd )
{
  struct sockaddr_in adresse_rpc ;
  int                sock = 0 ;
  CLIENT *           client ;
  struct timeval     intervalle ;
  int                rc ;
  
  memset( &adresse_rpc, 0, sizeof( adresse_rpc ) ) ;
  adresse_rpc.sin_port        = port ;
  adresse_rpc.sin_family      = AF_INET ;
  adresse_rpc.sin_addr.s_addr = adresse ;

  intervalle.tv_sec  = TIMEOUT_SEC ;
  intervalle.tv_usec = 0 ;
  
  sock = RPC_ANYSOCK ;
  

  /* Creation et allocation du client */

 
  if( ( client = clntudp_bufcreate( &adresse_rpc, 
				    programme, 
				    version,  
 				    intervalle,
                                    &sock, 
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
  unsigned int     rpc_service_num = MOUNTPROG ;
  //unsigned int     rpc_service_num = NFS_PROGRAM ;
  unsigned int     rpc_version = V3 ;
  int              val = 2 ;
  int              rc ;
  char             gss_service[1024] ;


  char dirpath[1024] ;

  char myname[1024] ;
  int n, node, count;

  strncpy( dirpath, "prot", 1024 ) ;

  gethostname(myname, sizeof(myname));
  MPI_Init (&argc, &argv);
  MPI_Comm_rank (MPI_COMM_WORLD, &node);
  MPI_Comm_size (MPI_COMM_WORLD, &count);

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
                  fprintf( stderr, "Erreur de gethostbynane errno=%u|%s\n", errno, strerror( errno ) ) ;
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
                  fprintf( stderr, "Impossible de resoudre le service %s", optarg ) ;
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

        case 'h':
        case '?':
        default:
          /* Affichage de l'aide en ligne */
          fprintf( stderr, utilisation, nom_exec ) ;
          exit( 0 ) ;
          break ;
        }
    }
  
  if( ( client = Creer_RPCClient( adresse_serveur, rpc_service_num, rpc_version, 0 , RPC_ANYSOCK ) ) == NULL )
    {
      char erreur[100] ;
      strcpy( erreur, clnt_spcreateerror( "Creation RPC" ) ) ;
      fprintf( stderr, "Creation RPC: %s", erreur ) ;
      exit( 1 ) ;
    }

  if( (client->cl_auth = authunix_create_default(  ) ) == NULL )
    {
      char erreur[100] ;
      strcpy( erreur, clnt_spcreateerror( "Creation AUTH" ) ) ;
      fprintf( stderr, "Creation AUTH: %s", erreur ) ;
      MPI_Finalize ();
      exit( 1 ) ; 
    }


  fprintf( stderr, "Node %d [%s] start\n", node, myname ) ;
  MPI_Barrier(MPI_COMM_WORLD);
  if( ( rc = clnt_call(  client, MOUNTPROC3_NULL,
                      	(xdrproc_t)xdr_void, (caddr_t)NULL,
                       	(xdrproc_t)xdr_void, (caddr_t)NULL,
                        intervalle ) ) != RPC_SUCCESS )
    {
      clnt_perror( client, "appel a  MOUNTPROC3_NULL\n" ) ;
      MPI_Finalize ();
      exit( 1 ) ; 
    }
  fprintf( stderr, "Node %d [%s] end OK\n", node, myname ) ;
  
  auth_destroy( client->cl_auth ) ;  
  clnt_destroy( client ) ;

  MPI_Finalize ();
  exit( 0 ) ; 
}

