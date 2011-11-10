#include<stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <string.h>
#include <errno.h>


int main( int argc, char * argv[] )
{
  int rc ;
  char file[MAXPATHLEN] ;
  char filehl[MAXPATHLEN] ;
  char cmd[MAXPATHLEN] ;

  if( argc != 2 )
    fprintf( stderr, "One argument is required to tell the path of the file to be used\n" ) ;

  strncpy( file, argv[1], MAXPATHLEN ) ;
  snprintf( filehl, MAXPATHLEN, "%s.hardlink", file ) ;

  rc = link( file, filehl ) ;
  printf( "link %s %s : rc=%d errno=(%u|%s)\n", file, filehl, rc, errno, strerror( errno ) ) ;

  rc = rename( file, filehl ) ;
  printf( "rename %s %s : rc=%d errno=(%u|%s)\n", file, filehl, rc, errno, strerror( errno ) ) ;

  snprintf( cmd, MAXPATHLEN, "ls -lid %s %s", file, filehl ) ;
  system( cmd ) ; 
}
