/* 
 * Les sources du serveur toto, mais avec de vrais morceaux de GSSAPI dedans
 */

char rcsid[] = "$Id: toto-server-gss.c,v 1.7 2003/10/03 07:22:30 deniel Exp $" ;

/* Un tas d'include pour avoir les bindings standards */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <gssapi/gssapi.h>    /* Header de la gssapi */
#ifdef HAVE_KRB5
#include <gssapi/gssapi_krb5.h>    /* Header de la gssapi */
#endif


/* Mes manies avec les ecritures d'adresse */
#define TraduireAdresse( adresse, dest )                   \
           sprintf( dest, "%d.%d.%d.%d",                   \
                  ( ntohl( adresse ) & 0xFF000000 ) >> 24, \
                  ( ntohl( adresse ) & 0x00FF0000 ) >> 16, \
                  ( ntohl( adresse ) & 0x0000FF00 ) >> 8,  \
                  ( ntohl( adresse ) & 0x000000FF ) )

#define LOGFILE_DEFAUT     "./toto-server.log"
#define MAX_CONN           32
#define NIVEAU_LOG_DEFAUT  NIV_CRIT
#define LENMSG             256
#define STRLEN             256
#define GSS_SERVICE_DEFAUT "toto" 

#define TOKEN_NOOP              (1<<0)
#define TOKEN_CONTEXT           (1<<1)
#define TOKEN_DATA              (1<<2)
#define TOKEN_MIC               (1<<3)
#define TOKEN_CONTEXT_NEXT      (1<<4)
#define TOKEN_WRAPPED           (1<<5)
#define TOKEN_ENCRYPTED         (1<<6)
#define TOKEN_SEND_MIC          (1<<7)
#define TOKEN_NOOP              (1<<0)
#define TOKEN_CONTEXT           (1<<1)
#define TOKEN_DATA              (1<<2)
#define TOKEN_MIC               (1<<3)
#define TOKEN_CONTEXT_NEXT      (1<<4)
#define TOKEN_WRAPPED           (1<<5)
#define TOKEN_ENCRYPTED         (1<<6)
#define TOKEN_SEND_MIC          (1<<7)

#define KEYTAB "/etc/krb5.keytab"

void sperror_gss( char * str, OM_uint32 major, OM_uint32 minor ) ;

/* les options pour getopt */
char options[] = "hL:P:S:" ;

/* L'aide en ligne */
char utilisation[] = 
"Utilisation: %s [-hLPM] \n"
"\t[-h]                   affiche cet aide en ligne\n"
"\t[-L <logfile>]         indique le fichier de log\n"
"\t[-P <port ou service>] indique le port ou le service a utiliser\n"
"\t[-S <service Auth>]    le service utilise par ls GSS-API\n" ;

/* Des variables globales (en general ecrites une seule fois, relues plusieurs) */
static char logfile_name[MAXPATHLEN] ;    /* Le chemin du fichier de log           */
unsigned short binding_port = 0 ;
char        gss_service[STRLEN] ;
gss_cred_id_t creds;

/*
 * La routine de negociation avec le client sous GSSAPI 
 */

int obtention_creds( char * service_name, gss_cred_id_t * pcreds )
{
     gss_buffer_desc name_buf;
     gss_name_t server_name;
     OM_uint32 maj_stat, min_stat;
     
     /* Je prepare un buffer avec le nom du service a chercher */
     name_buf.value = service_name;
     name_buf.length = strlen(name_buf.value) + 1; /* ATtention au +1 */

     
     if( ( maj_stat = gss_import_name( &min_stat, &name_buf, 
                                       (gss_OID)GSS_C_NT_HOSTBASED_SERVICE, 
				      &server_name ) ) != GSS_S_COMPLETE )
       {
         char msg[256] ;
         
         sperror_gss( msg, maj_stat, min_stat ) ;
         printf( "Importation par la GSS-API du nom %s impossible: %d|%d = %s\n", 
                                service_name, maj_stat, min_stat, msg ) ;
         exit( 1 ) ;
       }
     else
       printf( "Nom de service '%s' correctement importe\n", service_name ) ;

     /* Specific DCE: dire ou trouver le keytab */
#ifdef GSSDCE
     if( ( maj_stat = gssdce_register_acceptor_identity( &min_stat, server_name, NULL, KEYTAB ) ) != GSS_S_COMPLETE )
       {
         char msg[256] ;
         
         sperror_gss( msg, maj_stat, min_stat ) ;
         printf( "Erreur dans gssdce_register_acceptor_identity pour nom %s: %d|%d = %s\n", 
                                 service_name, maj_stat, min_stat, msg ) ;

         exit( 1 ) ;
       }
#endif

#ifdef HAVE_KRB5
     if( ( maj_stat = gsskrb5_register_acceptor_identity( KEYTAB ) ) != GSS_S_COMPLETE )
       {
         char msg[256] ;

         sperror_gss( msg, maj_stat, min_stat ) ;
         printf( "Erreur dans krb5_gss_register_acceptor_identity pour nom %s: %d|%d = %s\n",
                                 service_name, maj_stat, min_stat, msg ) ;

         exit( 1 ) ;
       }

#endif

     

     /* Obtention des creds avec ce nom gssapi-ifie */
     if( ( maj_stat = gss_acquire_cred( &min_stat, 
                                        server_name,
                                        0, 
                                        GSS_C_NULL_OID_SET, 
                                        GSS_C_ACCEPT,
                                        pcreds, 
                                        NULL, 
                                        NULL ) ) != GSS_S_COMPLETE )
       {
         char msg[256] ;

         sperror_gss( msg, maj_stat, min_stat ) ;
         printf( "Obtentiond des creds pour le nom %s impossible: %d|%d = %s\n", 
                                service_name, maj_stat, min_stat, msg ) ;
         exit( 1 ) ;
       }
     else
       printf( "Obtention des creds Ok pour le service %s\n", service_name ) ;
     
       
     return 0;
} /* obtention_creds */


gss_ctx_id_t    mon_contexte = GSS_C_NO_CONTEXT ;
int negociation_server( int sockfd )
{
  /*   gss_ctx_id_t    *cont = GSS_C_NO_CONTEXT ; */
  gss_buffer_desc client_name ;
  gss_buffer_desc send_tok ;
  gss_buffer_desc recv_tok ;
  gss_name_t      tname ;
  gss_name_t      client ;
  gss_OID         doid ;
  gss_buffer_desc oid_name ;
  int             token_flags;
  OM_uint32       maj_stat, min_stat ;
  OM_uint32       acc_sec_min_stat ;
  OM_uint32       ret_flags ;

  gss_ctx_id_t    toto = 0 ;
  

  printf( "Debut de negociation pour nouvelle connexion\n" ) ;
  
  /* 0 - Un token recu  pour commencer */
  if( recv_token( sockfd, &token_flags, &recv_tok ) < 0 )
    return -1 ;

  /* On relache le token, seuls les flags m'interessent */
  (void) gss_release_buffer(&min_stat, &recv_tok); 

  /* Je regarde les flags */
  if(!(token_flags & TOKEN_NOOP)) 
    {
      printf( "Erreur de token: NOOP attendu, %d recu a la place\n", token_flags ) ;
      return -1;
    }
  
  /* Je prepare le contexte */
  toto = GSS_C_NO_CONTEXT ;
  mon_contexte = GSS_C_NO_CONTEXT ;
 
  
  /* Boucle principale d'authentification */
  if (token_flags & TOKEN_CONTEXT_NEXT ) 
    {
      do
        {
          /* 1- Reception d'un token initial */
          if( recv_token( sockfd, &token_flags, &recv_tok ) < 0 )
            {
              printf( "Erreur de negociation, init passe: Mauvais token recu\n" ) ;
              return -1 ;
            }
          else
            printf( "Reception d'un token de taille %d\n", recv_tok.length ) ;
          
          /* 2- on utilise ce token pour accepter un contexte, ce qui cree un token a envoyer */
          maj_stat = gss_accept_sec_context( &acc_sec_min_stat, 
                                             &mon_contexte,
                                             creds, 
                                             &recv_tok, 
                                             GSS_C_NO_CHANNEL_BINDINGS,
                                             &client, 
                                             &doid, 
                                             &send_tok, 
                                             &ret_flags, 
                                             NULL,     /* ignore time_rec */
                                             NULL ) ;  /* ignore del_cred_handle */

          /* Je relache le recv_token, on n'en a plus besoin */
          (void) gss_release_buffer( &min_stat, &recv_tok ) ; 
          
          /* 3- Je donne le send_tok cree par l'appel precedent au client */
          if( send_token( sockfd, TOKEN_CONTEXT, &send_tok ) < 0 )
            {
              printf( "Erreur de negociation: mauvais envoi de token phase 2\n" ) ;
              return -1 ;
            }
          
          /* Je relache le send_token, j'en aurait besoin pour une eventuelle passe ulterieure */
          (void) gss_release_buffer(&min_stat, &send_tok); 

          /* Maintenant je regarde s'il faut faire un nouveau tour de table a la negociation */
          if( maj_stat != GSS_S_COMPLETE && maj_stat != GSS_S_CONTINUE_NEEDED  )
            {
              /* Une erreur de negociation s'est produite */
              char msg[STRLEN] ;
              
              sperror_gss( msg, maj_stat, acc_sec_min_stat ) ;
              printf( "Negociation impossible: %d|%d = %s\n", maj_stat, acc_sec_min_stat, msg ) ;

              /* Je n'ai pas de contexte, faire le nettoyage qui s'impose */
              if( mon_contexte == GSS_C_NO_CONTEXT )
                gss_delete_sec_context( &min_stat, &mon_contexte, GSS_C_NO_BUFFER ) ;
              
              return -1 ;
            }
          else if( maj_stat == GSS_S_CONTINUE_NEEDED )
            {
              printf( "Negociation: Il est necessaire de faire une nouvelle passe..\n" ) ;
            } 
            
        } while( maj_stat == GSS_S_CONTINUE_NEEDED ) ;
      
      if( ( maj_stat = gss_display_name( &min_stat, client, &client_name, &doid ) ) != GSS_S_COMPLETE )
        {
          char msg[STRLEN] ;
          
          sperror_gss( msg, maj_stat, min_stat ) ;
          printf( "Erreur de negociation: nom du client intraduisible: %d|%d = %s\n", maj_stat, min_stat, msg ) ;
          (void)gss_release_name(&min_stat, &client);
          return -1 ;
        }

      /* Devine qui viens diner ..... */
      printf( "Negociation Ok pour client %s\n", client_name.value ) ;
      
      
    }
  
  return 0 ;
} /* negociation */


/* Le main */
int main( int argc, char * argv[] )
{
  char               nom_exec[MAXPATHLEN] ;
  char *             tempo_nom_exec  = NULL ;
  int                c ;                     /* pour getopt                  */
  struct servent *   service = NULL ;        /* pour getservbyname           */
  struct sockaddr_in adresse ;
  struct sockaddr_in adresse_client ;
  unsigned long      longueur ;
  int                socket_service ;
  int                socket_ecoute ;
  int                one = 1 ;
  char               straddr[100] ;
  char               msg[LENMSG] ;
  char               msg_retour[LENMSG] ;
  int                rc = 0 ;
  char               serr[1024] ;
  
  while( ( c = getopt( argc, argv, options ) ) != EOF )
    {
      switch( c ) 
        {
        case 'L':
          /* On indique un fichier de log alternatif */
          strcpy( logfile_name, optarg ) ;
          break ;

        case 'P':
          /* Je m'enregistre sur un port fixe (pour eviter le portmapper) */
          /* Le numero de port peut etre un numero de port ou un service  */
          /* On a une valeur au format NET                                */
          if( isalpha( *optarg ) )
            {
              /* C'est un service car ca commence par une lettre */
              if( ( service = getservbyname( optarg, "tcp" ) ) == NULL )
                {
                  /* Ca marche pas en tcp, j'essaye en udp */
                  if( ( service = getservbyname( optarg, "udp" ) ) == NULL )
                    {
		      fprintf( stderr, "getservbyname failed: errno=%u|%s\n", errno, strerror( errno ) ) ;
                    }
                  else
                    {
                      binding_port = service->s_port ;
                    }
                }
              else
                {
                  binding_port = service->s_port ;
                }
            }
          else
            {
              /* C'est un numero de port */
              binding_port = htons( (unsigned short)atoi( optarg ) ) ;
            }
          break ;

        case 'S':
          /* Le nom de service */
          strcpy( gss_service, optarg ) ;
          break ;
          
        case 'h':
        case '?':
        default:
          /* Affichage de l'aide en ligne */
          fprintf( stderr, utilisation, nom_exec ) ;
          exit( 0 ) ;
          break ;
        } /* switch( c ) */
    } /* while( getopt( ... ) ) */
  
  if( argc != optind )
    {
      fprintf( stderr, utilisation, nom_exec ) ;
      fprintf( stderr, "Pas d'argument additionnel\n" ) ;
      exit( 1 ) ;
    }
      
  /* Petit coucou initial */
  printf( "Demarrage du serveur toto-server\n" ) ;
  printf( "Service GSSAPI = %s\n", gss_service ) ;
  
  /* Creation de la socket d'ecoute */
  if( ( socket_ecoute = socket( AF_INET, SOCK_STREAM, 0 ) ) == -1 )
    {
      fprintf( stderr, "Creation de socket impossible: errno=%u|%s\n", errno, strerror( errno ) ) ;
      exit( 1 ) ;
    }
  
  /* Quelques options bien utiles sur la socket */
  if( setsockopt( socket_ecoute,  SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof( one ) ) < 0 )
    {
      fprintf( stderr, "setsockopt impossible: errno=%u|%s\n", errno, strerror( errno ) ) ;
      exit( 1 ) ;
    }
  
  
  /* Attachement sur le port dedie a ce service */
  adresse.sin_family = AF_INET ;
  adresse.sin_port = binding_port  ;
  adresse.sin_addr.s_addr = INADDR_ANY  ; /* pas de discrimination de provenance a ce niveau */

  if( bind( socket_ecoute, (struct sockaddr *)&adresse, sizeof( adresse ) ) == -1 )
    {
      fprintf( stderr, "bind impossible: errno=%u|%s\n", errno, strerror( errno ) ) ;
      exit( 1 ) ;
    }
  
   /* Ecoute en attente de connexions */
      
  if( listen( socket_ecoute, MAX_CONN ) == -1 )
    {
      fprintf( stderr, "listen impossible: errno=%u|%s\n", errno, strerror( errno ) ) ;
      exit( 1 ) ;
    }

  /* Obtention des creds pour la GSSAPI */
  obtention_creds( gss_service, &creds ) ;

 

  /* Boucle principale de l'accept */
  printf( "En attente de connexion\n" ) ;
  printf( "------------------------\n" ) ;
  
  do
    {
      longueur = sizeof( adresse ) ;
      socket_service = accept( socket_ecoute, (struct sockaddr *)&adresse_client, (socklen_t *)&longueur ) ;
      
      if( socket_service == -1 && errno == EINTR )
        continue ; /* C'est pas une erreur trop grave, accept a ete interrompu par un signal */
      else
        if( socket_service == -1 ) /* Une erreur plus grave => on degage */
          {
            fprintf( stderr, "accept impossible: errno=%u|%s\n", errno, strerror( errno ) ) ;
            exit( 1 ) ;
          }

      /* A ce stade, on a une socket de service valide */
      TraduireAdresse( adresse_client.sin_addr.s_addr , straddr ) ;
      printf( "Une connexion entrante, source = %s:%d\n", straddr, ntohs( adresse_client.sin_port ) ) ;
      
       /* On negocie le contexte de securite avec le client a present */
      if( negociation_server( socket_service ) < 0 )
        continue ;
      
  
      /* Reception du message */
      /* if( ( rc = read( socket_service, msg, LENMSG ) ) != LENMSG ) */
      if( recv_msg( socket_service, msg, mon_contexte, serr ) < 0 ) 

        {
          fprintf( stderr, "%d octets emis au lieu de %d, errno=%u|%s", rc, LENMSG, errno, strerror( errno ) ) ;
          exit( 1 ) ;
        }
      printf( "Je recois le message : #%s#\n" , msg ) ;
      
      /* Je fais un message de retour */
      sprintf( msg_retour, "--->%s<---" ,  msg ) ;
      printf( "J'envoie le message : #%s#\n", msg_retour ) ;
      
      /* if( ( rc = write( socket_service, msg_retour, LENMSG ) ) != LENMSG )  */
      if( send_msg( socket_service, msg_retour, mon_contexte, serr ) < 0 ) 
        {
          fprintf( stderr, "%d octets envoyes au lieu de %d, errno=%u|%s", rc, LENMSG, errno, strerror( errno ) ) ;
          exit( 1 ) ;
        }
      
      /* Fermeture de la socket qui vient de servir */
      close( socket_service ) ;
      
    } while( 1 ) ;
  
}

