#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/param.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>



#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>

#include <gssrpc/types.h>
#include <gssrpc/xdr.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/auth_gss.h>
#include <gssrpc/clnt.h>

#include "nfs4.h"

#include <pthread.h>
#include <string.h>
#include <errno.h>


#define TraduireAdresse( adresse, dest )                   \
           sprintf( dest, "%d.%d.%d.%d",                   \
                  ( ntohl( adresse ) & 0xFF000000 ) >> 24, \
                  ( ntohl( adresse ) & 0x00FF0000 ) >> 16, \
                  ( ntohl( adresse ) & 0x0000FF00 ) >> 8,  \
                  ( ntohl( adresse ) & 0x000000FF ) )
#define TIMEOUT_SEC 25 
#define DEFAULT_RPC_SERVICE 100003 
#define SERVICE_NAME "toto@localhost" 
#define RECV_SIZE 2048
#define SEND_SIZE 2048
#define V4 4 
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
"\t[-p <rpc proc>]        indique le numero de function a utiliser\n"
"\t[-S <service GSSAPI>]  indique service GSSAPI a utiliser\n"  ;

void * Mem_Alloc( size_t size ) 
{
   return malloc( size ) ;
}

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
            fprintf( stderr, "connect impossible sur le serveur RPC" ) ;
        }
      else
        {
          /* Dans ce cas, on ne connait pas le port en face, donc connect impossible, on prend RPC_ANYSOCK 
           * mais uniquement apres avoir ferme la socket 'sock' qui ne sert a rien ici */
          close( sock ) ;
          sock = RPC_ANYSOCK ;
        }
    }
  

  /* Creation et allocation du client */
  
  if( ( client = clnttcp_create( &adresse_rpc, programme, version,  
                                 &sock, SEND_SIZE, RECV_SIZE ) ) == NULL )
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
  unsigned int     rpc_version = V4 ;
  int              val = 2 ;
  int              rc ;
  char             gss_service[1024] ;

  struct rpc_gss_sec rpcsec_gss_data ;
  gss_OID            mechOid ;
  char               mechname[1024] ;
  gss_buffer_desc    mechgssbuff ;
  OM_uint32          maj_stat, min_stat ;

  struct COMPOUND4args compound4_args ;
  struct COMPOUND4res  compound4_res ;

  strcpy( gss_service,  SERVICE_NAME ) ;

  
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

  compound4_args.tag.utf8string_len = 0 ; /* No Tag */
  compound4_args.minorversion = 0 ;
  compound4_args.argarray.argarray_len = 1 ;
  compound4_args.argarray.argarray_val = (struct nfs_argop4 * )Mem_Alloc( sizeof( struct nfs_argop4 ) ) ;
  compound4_args.argarray.argarray_val[0].argop = NFS4_OP_PUTROOTFH ; /* This operation requires no argument */

  /* Set up mechOid */ 
  strcpy( mechname, "{ 1 2 840 113554 1 2 2 }"  ) ;

  mechgssbuff.value = mechname ;
  mechgssbuff.length = strlen( mechgssbuff.value ) ;  

  if( ( maj_stat = gss_str_to_oid( &min_stat, &mechgssbuff, &mechOid ) ) != GSS_S_COMPLETE )
    {
        fprintf( stderr, "str_to_oid %u|%u", maj_stat, min_stat);
        exit( 1 ) ;
    }

  
  /* Authentification avec RPCSEC_GSS */
  rpcsec_gss_data.mech = mechOid ;
  rpcsec_gss_data.qop =  GSS_C_QOP_DEFAULT ;
  /* rpcsec_gss_data.svc = RPCSEC_GSS_SVC_NONE ; */
  /* rpcsec_gss_data.svc = RPCSEC_GSS_SVC_INTEGRITY ; */
  rpcsec_gss_data.svc = RPCSEC_GSS_SVC_PRIVACY ; 

  if( (client->cl_auth = authgss_create_default( client, gss_service, &rpcsec_gss_data ) ) == NULL )
    {
      char erreur[100] ;
      strcpy( erreur, clnt_spcreateerror( "Creation AUTHGSS" ) ) ;
      fprintf( stderr, "Creation AUTHGSS: %s", erreur ) ;
      exit( 1 ) ;
    }

  val = 2 ;
  fprintf( stderr, "requete v4\n" ) ;
  if( ( rc = clnt_call(  client, 1,
                      	(xdrproc_t)xdr_COMPOUND4args, (caddr_t)&compound4_args, 
                       	(xdrproc_t)xdr_COMPOUND4res, (caddr_t)&compound4_res, 
                        intervalle ) ) != RPC_SUCCESS )
    {
      clnt_perror( client, "appel a  NFSPROC4_COMPOUND\n" ) ;
      exit ( 1 ) ;
    }
  fprintf( stderr, "Requete v4 OK\n"  ) ;
  
  auth_destroy( client->cl_auth ) ;  
  clnt_destroy( client ) ;
}

