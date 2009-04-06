/*
 *
 * Une boite a outils pour utiliser la GSSAPI 
 *
 */

char rcsid_tools[] = "$Id: tools-gss.c,v 1.4 2003/10/03 08:15:11 deniel Exp $" ;

/* Un tas d'include pour avoir les bindings standards */
#include <sys/time.h>
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

#define gss_wrap gss_seal
#define gss_unwrap gss_unseal 
#define gss_get_mic gss_sign 
#define gss_verify_mic gss_verify
#define gss_qop_t OM_uint32 

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

/* 
 * convertir une erreur GSSAPI Major/Minor en un message lisible 
 */

void sperror_gss_1( char * str, OM_uint32 code, int type )
{
  gss_buffer_desc  msg = GSS_C_EMPTY_BUFFER  ;
  OM_uint32 msg_ctx   ;
  OM_uint32 maj_stat, min_stat ;
  
  msg_ctx = 0 ;
  while ( 1 ) 
    {
      if( ( maj_stat = gss_display_status( &min_stat, code, type, GSS_C_NULL_OID, &msg_ctx, &msg ) ) != GSS_S_COMPLETE )
        {
          sprintf( str, "Erreur %d intraduisible par gss_display_status: code retour = %d.%d", 
                   code, maj_stat, min_stat ) ;
          break ;
        }
      else
        sprintf( str, "GSSAPI-ERROR %d = %s", code, (char *)msg.value ) ;
      
      if( msg.length != 0 )
        gss_release_buffer( &min_stat, &msg ) ;
      
      if( !msg_ctx )
        break ;
    }     
}  /* sperror_gss_1 */

void sperror_gss( char * str, OM_uint32 maj_stat, OM_uint32 min_stat )
{
  char str1[256] ;
  char str2[256] ;
  sperror_gss_1( str1, maj_stat, GSS_C_GSS_CODE ) ;
  sperror_gss_1( str2, min_stat, GSS_C_MECH_CODE ) ;
  
  sprintf( str, "%s ; %s", str1, str2 ) ;
  
}  /* sperror_gss */
static int write_all(int fildes, char *buf, unsigned int nbyte)
{
     int ret;
     char *ptr;

     for (ptr = buf; nbyte; ptr += ret, nbyte -= ret) {
	  ret = send(fildes, ptr, nbyte, 0);
	  if (ret < 0) {
	       if (errno == EINTR)
		    continue;
	       return(ret);
	  } else if (ret == 0) {
	       return(ptr-buf);
	  }
     }

     return(ptr-buf);
}

static int read_all(int fildes, char *buf, unsigned int nbyte)
{
     int ret;
     char *ptr;

     for (ptr = buf; nbyte; ptr += ret, nbyte -= ret) {
	  ret = recv(fildes, ptr, nbyte, 0);
	  if (ret < 0) {
	       if (errno == EINTR)
		    continue;
	       return(ret);
	  } else if (ret == 0) {
	       return(ptr-buf);
	  }
     }

     return(ptr-buf);
}

/*
 * Function: send_token
 *
 * Purpose: Writes a token to a file descriptor.
 *
 * Arguments:
 *
 * 	s		(r) an open file descriptor
 *	flags		(r) the flags to write
 * 	tok		(r) the token to write
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 *
 * send_token writes the token flags (a single byte, even though
 * they're passed in in an integer), then the token length (as a
 * network long) and then the token data to the file descriptor s.  It
 * returns 0 on success, and -1 if an error occurs or if it could not
 * write all the data.
 */
int send_token(int s, int flags, gss_buffer_t tok)
{
  int len, ret;
  unsigned char char_flags = (unsigned char) flags;
  
  ret = write_all(s, (char *)&char_flags, 1);
  if (ret != 1) {
    perror("sending token flags");
    return -1;
  }
  
  len = htonl(tok->length);

  ret = write_all(s, (char *) &len, 4);
  if (ret < 0)
    {
      perror("sending token length");
      return -1;
    } 
  else if (ret != 4)
    {
      if (stderr)
        fprintf(stderr,"sending token length: %d of %d bytes written\n", 
                ret, 4);
      return -1;
    }
  
  printf( "send_token   tok->length = %d\n", tok->length ) ;

  ret = write_all(s, tok->value, tok->length);
  if (ret < 0) 
    {
      perror("sending token data");
      return -1;
    } 
  else if (ret != tok->length) 
    {
      if (stderr)
        fprintf( stderr, "sending token data: %d of %d bytes written\n", 
                 ret, tok->length);
      return -1;
    }
  
  return 0;
}

/*
 * Function: recv_token
 *
 * Purpose: Reads a token from a file descriptor.
 *
 * Arguments:
 *
 * 	s		(r) an open file descriptor
 *	flags		(w) the read flags
 * 	tok		(w) the read token
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 * 
 * recv_token reads the token flags (a single byte, even though
 * they're stored into an integer, then reads the token length (as a
 * network long), allocates memory to hold the data, and then reads
 * the token data from the file descriptor s.  It blocks to read the
 * length and data, if necessary.  On a successful return, the token
 * should be freed with gss_release_buffer.  It returns 0 on success,
 * and -1 if an error occurs or if it could not read all the data.
 */
int recv_token( int s, int * flags, gss_buffer_t tok)
{
     int ret;
     unsigned char char_flags;

     ret = read_all(s, (char *) &char_flags, 1);
     if (ret < 0)
       {
         perror("reading token flags");
         return -1;
       } 
     else if (! ret)
       {
         if (stderr)
           fputs("reading token flags: 0 bytes read\n", stderr);
         return -1;
       } 
     else 
       {
         *flags = (int) char_flags;
       }

     ret = read_all(s, (char *) &tok->length, 4);
     if (ret < 0)
       {
         perror("reading token length");
         return -1;
       } 
     else if (ret != 4)
       {
         if (stderr)
           fprintf(stderr, 
                   "reading token length: %d of %d bytes read\n", 
                   ret, 4);
         return -1;
       }
     
     tok->length = ntohl(tok->length);
     tok->value = (char *) malloc(tok->length); 
     if (tok->length && tok->value == NULL)
       {
         if (stderr)
           fprintf(stderr, 
                   "Out of memory allocating token data\n");
         return -1;
       }

     printf( "recv_token   tok->length = %d\n", tok->length ) ;

     ret = read_all(s, (char *) tok->value, tok->length);
     if (ret < 0)
       {
         perror("reading token data");
         free( tok->value ) ;
         return -1;
       } 
     else if (ret != tok->length)
       {
         fprintf(stderr, "sending token data: %d of %d bytes written\n", 
                 ret, tok->length);
         free(tok->value);
         return -1;
       }
     
     return 0;
}

int send_msg( int fd, char * msg, gss_ctx_id_t context, char * errbuff )
{
  gss_buffer_desc clear_buf ;
  gss_buffer_desc code_buf ;
  gss_buffer_desc mic_buf ;
  OM_uint32       maj_stat, min_stat ;
  int             encrypt_flag = 1 ;
  int             state ;
  int             tok_flags ;
  gss_qop_t		    qop_state;
  
  clear_buf.value = msg ;
  clear_buf.length = strlen( msg ) +1  ; /* pour garder le \0 final */

  /* Wrapping du message */
  if( ( maj_stat = gss_wrap( &min_stat, 
                             context, 
                             encrypt_flag, 
                             GSS_C_QOP_DEFAULT,
                             &clear_buf, 
                             &state, 
                             &code_buf ) ) != GSS_S_COMPLETE )
    {
      sperror_gss( errbuff, maj_stat, min_stat ) ;
      printf( "erreur gss_wrap \n" ) ;
      return -1 ;
    }

  if( !state )
    {
      sprintf( errbuff, "buffer non encode !!" ) ;
      return -1 ;
    }

  /* Envoi de la chose encode */
  if( send_token( fd, (TOKEN_DATA|TOKEN_WRAPPED|TOKEN_ENCRYPTED), &code_buf ) < 0 )
    {
      sprintf( errbuff, "pb dans send_token" ) ;
      return -1 ;
    }
  
  /* reception de la mic */
  if( recv_token( fd, &tok_flags, &mic_buf ) )
    {
      sprintf( errbuff, "pb dans recv_token" ) ;
      return -1 ;
    }

  /* Verification de la mic */
  if( ( maj_stat = gss_verify_mic( &min_stat, 
                                   context, 
                                   &clear_buf,
                                   &mic_buf , 
                                   &qop_state ) ) != GSS_S_COMPLETE )
    {
      sperror_gss( errbuff, maj_stat, min_stat ) ;
      printf( "Erreur gss_verify_mic\n" ) ;
      return -1 ;
    }

  printf( "-----> gss_verify_mic OK\n" ) ;
  
  return 0 ;
}

int recv_msg( int fd, char * msg, gss_ctx_id_t context, char * errbuff ) 
{
  gss_buffer_desc code_buf ;
  gss_buffer_desc clear_buf ;
  gss_buffer_desc mic_buf ;
  int             ret_flags ;
  int             conf_state ;
  OM_uint32       maj_stat, min_stat ;
  
  if( recv_token( fd, &ret_flags, &code_buf ) < 0 )
    {
      sprintf( errbuff, "erreur dans recv_token" ) ;
      return -1 ;
    }
  
  /* Unwrapping du message */
  if( ( maj_stat = gss_unwrap( &min_stat, 
                               context, 
                               &code_buf,
                               &clear_buf, 
                               &conf_state, 
                               (OM_uint32 *)NULL ) ) != GSS_S_COMPLETE )
    {
      sperror_gss( errbuff, maj_stat, min_stat ) ;
      return -1 ;
    }

  if( !conf_state )
    {
      sprintf( errbuff, "message non encrypte" ) ;
    }
  /* Calcul de la mic */
  if( ( maj_stat = gss_get_mic( &min_stat, 
                                context, 
                                GSS_C_QOP_DEFAULT, 
                                &clear_buf, 
                                &mic_buf ) ) != GSS_S_COMPLETE )
    {
      sperror_gss( errbuff, maj_stat, min_stat ) ;
      return -1 ;
    }
 
  printf( "----> gss_get_mic OK\n" ) ;
 
  if( send_token( fd, TOKEN_MIC, &mic_buf ) < 0 )
    {
      sprintf( errbuff, "erreur dans send_token" ) ;
      return -1 ;
    } 

  strncpy( msg, clear_buf.value, clear_buf.length ) ;

  return 0;
}
