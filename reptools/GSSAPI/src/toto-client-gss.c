/*
 *
 * Les sources du client du serveur toto
 *
 */

char rcsid[] = "$Id: toto-client-gss.c,v 1.3 2003/10/02 13:57:45 deniel Exp $" ;

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

#include <gssapi/gssapi.h>  /* Header de la gssapi */
#ifdef HAVE_KRB5
#include <gssapi/gssapi_krb5.h>  /* Header de la gssapi */
#endif

#define TOKEN_NOOP              (1<<0)
#define TOKEN_CONTEXT           (1<<1)
#define TOKEN_DATA              (1<<2)
#define TOKEN_MIC               (1<<3)
#define TOKEN_CONTEXT_NEXT      (1<<4)
#define TOKEN_WRAPPED           (1<<5)
#define TOKEN_ENCRYPTED         (1<<6)
#define TOKEN_SEND_MIC          (1<<7)

void sperror_gss( char * str, OM_uint32 major, OM_uint32 minor ) ;
int write_tok( int s, gss_buffer_t tok ) ;
int read_tok( int s, gss_buffer_t tok ) ;

#define LENMSG            256 
#define TraduireAdresse( adresse, dest )                   \
           sprintf( dest, "%d.%d.%d.%d",                   \
                  ( ntohl( adresse ) & 0xFF000000 ) >> 24, \
                  ( ntohl( adresse ) & 0x00FF0000 ) >> 16, \
                  ( ntohl( adresse ) & 0x0000FF00 ) >> 8,  \
                  ( ntohl( adresse ) & 0x000000FF ) )
#define SNAME_DEFAUT "toto"

/* les options pour getopt */
char options[] = "hd:P:M:S:" ;

/* L'aide en ligne */
char utilisation[] =
"Utilisation: %s [-hdPM] message\n"
"\t[-h]                   affiche cet aide en ligbe\n"
"\t[-d <machine>]         indique la machine serveur\n"
"\t[-P <port ou service>] le port ou le service ou le daemon ecoute\n"
"\t[-M <Mecanisme Auth>]  le mecanisme utilise par ls GSS-API\n" 
"\t[-S <Mecanisme Auth>]  le service utilise par ls GSS-API\n" ;


/* Quelques variables d'etat pour la GSSAPI */
OM_uint32         deleg_flag = 0 ;
gss_ctx_id_t    gss_context ;
gss_OID         g_mechOid ;
char            sname[256] ;

/* Cette routine alloue une adresse et la lie a son point d'entree                   */
/* La valeur retournee est le descripteur de socket ou une valeur negative si erreur */
/* si port == 0, on choisit dynamiquement le port                                    */
/* adresse et port sont au format NET */
int CreerSocket( unsigned int adresse, unsigned short port )
{
  int                socket_fd ;
  struct sockaddr_in addr ;
  
  /* 1- On cree la socket  */
  if( ( socket_fd = socket( AF_INET, SOCK_STREAM, 0 ) ) == -1 )
    {
      fprintf( stderr, "error socket: errno=%u|%s\n", errno, strerror( errno ) ) ;
      return -1 ;
    }

  /* 2- On remplit l'adresse */
  memset( &addr, 0, sizeof( addr ) ) ;
  addr.sin_family      = AF_INET ;
  addr.sin_addr.s_addr = adresse ;
  addr.sin_port        = port  ;

  /* 3- On bind la socket sur l'adresse */
  if( bind( socket_fd, (struct sockaddr *)&addr, sizeof( addr ) ) == -1 )
    {
      fprintf( stderr, "error socket: errno=%u|%s\n", errno, strerror( errno ) ) ;
      return -1 ;
    }

  /* 4- on s'en va */
  return socket_fd ;
} /* CreerSocket */
 

/*
 * La routine de negociation avec le serveur sous GSSAPI 
 */
int negociation_client( int sockfd, char * service_name, gss_OID oid  ) 
{
  gss_buffer_desc   send_tok ;
  gss_buffer_desc   recv_tok ;
  gss_buffer_desc * token_ptr ;
  
  gss_name_t      tname ;
  OM_uint32       maj_stat, min_stat, init_sec_min_stat ;
  OM_uint32       ret_flags ;
  int             token_flags;

  char            strerrgss[256] ;
  gss_buffer_desc empty_token = { 0, (void *) "" };
  
  /* Je dois construire le target name dans un format GSSAPI compliant */
  fprintf( stderr, "Negociation pour acceder au service '%s'\n", service_name ) ;
  
  send_tok.value = service_name ;
  send_tok.length = strlen( service_name ) + 1 ;

  /* Instanciation du nom GSSAPI du service */
  if( ( maj_stat = gss_import_name( &min_stat, &send_tok, 
                                    (gss_OID)GSS_C_NT_HOSTBASED_SERVICE, &tname ) ) != GSS_S_COMPLETE )
    {
      sperror_gss( strerrgss, maj_stat, min_stat ) ;
      fprintf( stderr, "gss_import_name: %s\n", strerrgss ) ;
      exit( 1 ) ;
    }
  
  /* 0- On envoie un toekn vide pour commencer, juste histoire de donner des flags */
  if (send_token(sockfd, TOKEN_NOOP|TOKEN_CONTEXT_NEXT, &empty_token) < 0)
    {
      fprintf( stderr, "send_token: errno=%d\n", errno ) ;
      (void) gss_release_name(&min_stat, &tname);
      exit( 1 ) ; 
    }
  
  /* Boucle d'etablissement du contexte */
  token_ptr = GSS_C_NO_BUFFER ;
  gss_context = GSS_C_NO_CONTEXT ;
  
  do
    {
      /* On cree un context que l'on va envoyer au serveur */
      maj_stat = gss_init_sec_context( &init_sec_min_stat, 
                                       GSS_C_NO_CREDENTIAL, 
                                       &gss_context, 
                                       tname, 
                                       oid,  
                                       GSS_C_MUTUAL_FLAG | GSS_C_REPLAY_FLAG | deleg_flag, 
                                       0, 
                                       GSS_C_NO_CHANNEL_BINDINGS,         /* no channel bindings */
                                       token_ptr,    /* ignore mech type    */
                                       NULL, 
                                       &send_tok, 
                                       (int *)&ret_flags, 
                                       NULL ) ;      /* ignore time_rec     */
      
      if( token_ptr != GSS_C_NO_BUFFER )
        (void) gss_release_buffer( &min_stat, &recv_tok ) ;

      if( send_tok.length != 0 )
        {
          fprintf( stderr, "Envoie du contexte initial, taille=%d\n", send_tok.length ) ;
          
          if( send_token( sockfd, TOKEN_CONTEXT, &send_tok ) < 0 )
            {
              fprintf( stderr, "Erreur a l'envoie du contexte initiale\n" ) ;
              (void) gss_release_buffer(&min_stat, &send_tok);
              (void) gss_release_name(&min_stat, &tname);
              exit( 1 ) ;
            }
          
            
        }
      else 
        fprintf( stderr, "Le contexte intiale a une taille nulle\n" ) ;

      /* Maintenant que send_tok est envoye, je le relache */
      (void) gss_release_buffer(&min_stat, &send_tok);
      
      if( maj_stat != GSS_S_COMPLETE && maj_stat != GSS_S_CONTINUE_NEEDED )
        {
          /* Une erreur a eu lieu dans gss_init_sec_context */

          sperror_gss( strerrgss, maj_stat, init_sec_min_stat ) ;
          fprintf( stderr, "gss_init_sec_context: %s\n", strerrgss ) ;
          (void) gss_release_name( &min_stat, &tname ) ;
          return -1 ;
        }
      
      /* Est ce que j'ai besoin de continuer ? */
      if( maj_stat == GSS_S_CONTINUE_NEEDED )
        {
          fprintf( stderr, "Une autre passe est necessaire\n" ) ;
          
          /* Je recupere dans recv_tok les infos venues du serveur */
          if( recv_token( sockfd, &token_flags, &recv_tok ) < 0 )
            {
              fprintf( stderr, "Erreur de recv_token sur la socket du serveur\n" ) ;
              gss_release_name( &min_stat, &tname ) ;
                  return -1 ;
                }
              
              /* Sinon, on utilise ce token recue pour la passe d'apres */
              token_ptr = &recv_tok ;
        }
      
    } while( maj_stat == GSS_S_CONTINUE_NEEDED ) ;
  
  fprintf( stdout, "Contexte de securite negocie...\n" ) ;  
  
  /* Un peu de menage */
  (void) gss_release_name(&min_stat, &tname);
  
  return 0 ;
} /* negociation */



/* Le main .... Et Loire (desole) */
int main( int argc, char * argv[] ) 
{ 
  int                c ;                     /* pour getopt                  */
  unsigned int       adresse_serveur ;  /* Au format NET */
  struct hostent *   hp ;
  char               nom_exec[MAXPATHLEN] ;
  char               machine_locale[256] ;
  char *             tempo_nom_exec  = NULL ;
  struct sockaddr_in addr_serveur ;
  int                sockfd ;
  unsigned short     serveur_port ;
  struct servent *   service ;
  char               straddr[100] ;
  char               msg[LENMSG] ;
  int                rc = 0 ;
  gss_buffer_desc    tokoid ;
  OM_uint32          maj_stat, min_stat ;
  char               strerrgss[256] ;
  char *             mechstr = 0 ;
  char *             cp ;
 
  gss_name_t       src_name, targ_name;
  gss_buffer_desc  s_name, t_name;
  OM_uint32        lifetime;
  OM_uint32        context_flags;
  gss_OID          mechanism, name_type;
  int              is_local, is_open ;
  gss_buffer_desc  oid_name ;
  gss_OID_set      mech_names ;
  int              i ;
  char             serr[1024] ;
 
  /* On recupere le nom de l'executable */
  if( ( tempo_nom_exec = strrchr( argv[0], '/' ) ) != NULL )
    strcpy( (char *)nom_exec, tempo_nom_exec + 1 ) ;

  /* Valeur par defaut */
  strcpy( sname, SNAME_DEFAUT ) ;
  
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
                  fprintf( stderr, "Error gethostbyname: errno=%u|%s\n", errno, strerror( errno ) ) ;
                  exit( 1 ) ; 
                }
              
              memcpy( &adresse_serveur, hp->h_addr, hp->h_length ) ;
            }
          else
            {
              adresse_serveur = inet_addr( optarg ) ;
            }
          break;

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
                      fprintf( stderr, "Error getservbyname: errno=%u|%s\n", errno, strerror( errno ) ) ;
                    }
                  else
                    {
                      serveur_port = service->s_port ;
                    }
                }
              else
                {
                  serveur_port = service->s_port ;
                }
            }
          else
            {
              /* C'est un numero de port */
              serveur_port = htons( (unsigned short)atoi( optarg ) ) ;
            }
          break ;
          
        case 'S':
          /* Nom de service GSSAPI */
          strcpy( sname, optarg ) ;
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
  
  if( argc - optind != 1 )
    {
      fprintf( stderr, utilisation, nom_exec ) ;
      fprintf( stderr,"Un seul argument additionel: le message\n" ) ;
      exit( 1 ) ;
    }

  /* Je garde le message */
  strncpy( msg, argv[optind], LENMSG ) ;
  
  
  /* Obtention du nom de la machine */
  if( gethostname( machine_locale, sizeof( machine_locale ) ) != 0 )
    {
      fprintf( stderr, "Error gethostname: errno=%u|%s\n", errno, strerror( errno ) ) ;
      exit( 1 ) ;
    }

  /* Qui est ce que je veux contacter ? */
  TraduireAdresse( adresse_serveur , straddr ) ;
  fprintf( stderr, "Je cherche a joindre %s:%d\n", straddr, ntohs( serveur_port ) ) ;
  
  /* Je creer une socket */
  if( ( sockfd = CreerSocket( htonl( INADDR_ANY ) , 0 ) ) == -1 )
    {
      fprintf( stderr, "Creation de la socket impossible, abandon...\n" ) ;
      exit( 1 ) ;
    }

  /* Je me connecte sur le serveur */
  addr_serveur.sin_family      = AF_INET ;
  addr_serveur.sin_port        = serveur_port ;
  addr_serveur.sin_addr.s_addr = adresse_serveur ;
  
  
  if( connect( sockfd, (struct sockaddr *)&addr_serveur, sizeof( addr_serveur ) ) )
    {
      fprintf( stderr, "Error connect: errno=%u|%s\n", errno, strerror( errno ) ) ;
      fprintf( stderr, "Le serveur est indisponible\n" ) ;
      exit( 1 ) ;
    }

  /* Si je suis ici, j'ai un connect correct */
  fprintf( stderr, "Connexion ok sur le serveur" ) ;

  /* Negociation avec la GSS-API */
  if( negociation_client( sockfd, sname, g_mechOid ) <0 )
    {
      fprintf( stderr, "Erreur de negociation avec le serveur, sortie\n" ) ;
      exit( 1 ) ;
    }
  else
    fprintf( stderr, "Negociation ok\n" ) ;
 
  maj_stat = gss_inquire_context( &min_stat, 
				  gss_context,
                                  &src_name, 
                                  &targ_name, 
                                  &lifetime,
                                  &mechanism, 
                                  &context_flags,
                                  &is_local, 
                                  &is_open);
  if (maj_stat != GSS_S_COMPLETE)
    {
        fprintf( stderr, "inquiring context %d|%d\n", maj_stat, min_stat);
        exit( 1 ) ;
    }

   maj_stat = gss_display_name(&min_stat, src_name, &s_name, &name_type);
   if (maj_stat != GSS_S_COMPLETE)
     {
            fprintf( stderr, "displaying source name %d|%d\n", maj_stat, min_stat);
            exit( 1 ) ;
      }
   maj_stat = gss_display_name(&min_stat, targ_name, &t_name,
                              (gss_OID *) NULL);
   if (maj_stat != GSS_S_COMPLETE)
      {
           fprintf( stderr, "displaying target name %d|%d\n", maj_stat, min_stat);
           exit( 1 ) ;
      }

  fprintf( stdout, "\"%.*s\" to \"%.*s\", lifetime %d, flags %x, %s, %s\n",
               (int) s_name.length, (char *) s_name.value,
               (int) t_name.length, (char *) t_name.value, lifetime,
               context_flags,
               (is_local) ? "locally initiated" : "remotely initiated",
               (is_open) ? "open" : "closed");

        (void) gss_release_name(&min_stat, &src_name);
        (void) gss_release_name(&min_stat, &targ_name);
        (void) gss_release_buffer(&min_stat, &s_name);
        (void) gss_release_buffer(&min_stat, &t_name);

  maj_stat = gss_oid_to_str(&min_stat, name_type, &oid_name);
  if (maj_stat != GSS_S_COMPLETE)
    {
       fprintf( stderr, "converting oid->string %d|%d\n", maj_stat, min_stat);
       exit( 1 ) ;
    }
   fprintf( stdout, "Name type of source name is %.*s.\n",
                       (int) oid_name.length, (char *) oid_name.value);
   (void) gss_release_buffer(&min_stat, &oid_name);


  /* Now get the names supported by the mechanism */
  maj_stat = gss_inquire_names_for_mech(&min_stat,
                                         mechanism, &mech_names);
  if (maj_stat != GSS_S_COMPLETE)
    {
        fprintf( stdout, "inquiring mech names %d|%d\n", maj_stat, min_stat);
        exit( 1 ) ;
     }

  maj_stat = gss_oid_to_str(&min_stat, mechanism, &oid_name);
  if (maj_stat != GSS_S_COMPLETE) 
     {
        fprintf( stdout, "converting oid->string %d|%d\n", maj_stat, min_stat);
        exit( 1 ) ;
     }
  fprintf( stdout, "Mechanism %.*s supports %d names\n",
                     (int) oid_name.length, (char *) oid_name.value,
                     (int) mech_names->count);
                     (void) gss_release_buffer(&min_stat, &oid_name);

  for (i = 0; i < mech_names->count; i++) 
    {
        maj_stat = gss_oid_to_str(&min_stat,
                                  &mech_names->elements[i], &oid_name);
        if (maj_stat != GSS_S_COMPLETE)
         {
           fprintf( stderr, "converting oid->string %d|%d\n", maj_stat, min_stat);
           exit( 1 ) ;
         }
         fprintf( stdout, "  %d: %.*s\n", (int) i,
                             (int) oid_name.length, (char *) oid_name.value);

         (void) gss_release_buffer(&min_stat, &oid_name);
    }
  (void)gss_release_oid_set(&min_stat, &mech_names);

 
  /* Envoi du message */
  /* if( ( rc = write( sockfd, msg, LENMSG ) ) != LENMSG )  */
  if( send_msg( sockfd, msg, gss_context, serr ) < 0 ) 
    {
      fprintf( stderr, "Error send: errno=%u|%s\n", errno, strerror( errno ) ) ;
      fprintf( stderr, "%d octets envoyes au lieu de %d\n", rc, LENMSG ) ;
      exit( 1 ) ;

    }
  
  fprintf( stderr, "Envoi du message #%s#\n", msg ) ;

  /* if( ( rc = read( sockfd, msg, LENMSG ) ) != LENMSG ) */
  if( recv_msg( sockfd, msg, gss_context, serr ) < 0 ) 
    {
      fprintf( stderr, "Error recv: errno=%u|%s\n", errno, strerror( errno ) ) ;
      fprintf( stderr, "%d octets emis au lieu de %d\n", rc, LENMSG ) ;
      exit( 1 ) ;
    }
  
  fprintf( stderr, "En retour j'ai le message #%s#\n", msg ) ;  

  /* fin */
  close( sockfd ) ;
  
  return 0 ;
}
