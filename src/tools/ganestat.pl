#!/bin/env perl
#
################## Doxygen header ##################
#*
#* \file        ganestat.pl
#* \author      $Author: leibovic $
#* \date        $Date: 2006/02/22 12:03:00 $
#* \version     $Revision: 1.4 $
#* \brief       print GANESHA's statistics in a human-readable way.
#*
####################################################

use English '-no_match_vars';
use File::Basename;
use Getopt::Std;

#### global vars ####

my $TAIL="/usr/bin/tail";

#### functions indexes arrays ####

my @cache_inode_fn_names =
  ( "access", "getattr", "mkdir", "remove", "statfs", "link",
    "readdir", "rename", "symlink", "create", "lookup", "lookupp", "readlink",
    "truncate", "get", "release", "setattr", "new_entry", "read_data",
    "write_data", "add_data_cache", "rel_data_cache", "renew_entry","lock_create",
    "lock", "locku", "lockt", "add_state", "del_state", "get_state", "set_state", "update_state" );


my @cache_inode_hash_fn_names =  ( "set", "test", "get", "del" );

my @mount_fn_names = ( "null", "mount", "dump", "umount", "umountall", "export" );

my @rquota_fn_names = ( "null", "getquota", "getquotaspecific", "setquota", "setquotaspecific" ) ;

my @nfs2_fn_names =
( "null", "getattr", "setattr", "root", "lookup", "readlink", "read", "writecache",
"write", "create", "remove", "rename", "link", "symlink", "mkdir", "rmdir",
"readdir", "statfs" );

my @nfs3_fn_names =
( "null", "getattr", "setattr", "lookup", "access", "readlink",
"read", "write", "create", "mkdir", "symlink", "mknod",
"remove", "rmdir", "rename", "link", "readdir", "readdirplus",
"fsstat", "fsinfo", "pathconf", "commit" );

my @nfs4_fn_names = ( "null", "compound" );


my @nfs40_op_names = 
( "n/a", "n/a", "n/a", "access", "close", "commit", "create",
"delegpurge", "delegreturn", "getattr", "getfh", "link", 
"lock", "lockt", "locku", "lookup", "lookupp", "nverify", "open",
"openattr", "open_confirm", "open_downgrade", "putfh", "putpubfh",
"putrootfh", "read", "readdir", "readlink", "remove", "rename", 
"renew", "restorefh", "savefh", "secinfo", "setattr", "setclientid",
"setclientid_confirm", "verify", "write", "release_lockowner" ) ;

my @nfs41_op_names = 
( "n/a", "n/a", "n/a", "access", "close", "commit", "create",
"delegpurge", "delegreturn", "getattr", "getfh", "link", 
"lock", "lockt", "locku", "lookup", "lookupp", "nverify", "open",
"openattr", "open_confirm", "open_downgrade", "putfh", "putpubfh",
"putrootfh", "read", "readdir", "readlink", "remove", "rename", 
"renew", "restorefh", "savefh", "secinfo", "setattr", "setclientid",
"setclientid_confirm", "verify", "write", "release_lockowner", 
"backchanelle_ctl", "bind_conn_to_session", "exchange_id", 
"create_session", "destroy_session", "free_stateid", "get_dir_delegation",
"getdeviceinfo", "getdevicelist", "layoutcommit", "layoutget", "layoutreturn",
"secinfo_no_name", "sequence", "set_ssv", "test_stateid", "want_delegation",
"destroy_clientid", "reclaim_complete" ) ;


my @nlm_fn_names = ( "null", "test", "lock", "cancel", "unlock", "granted", "test_msg", "lock_msg", 
  "cancel_msg", "unlock_msg", "granted_msg", "test_res", "lock_res", "cancel_res", "unlock_res", "granted_res",
  "sm_notify", "m/a", "n/a", "n/a", "share", "unshare", "nm_lock", "free_all" ) ;

my @fsal_fn_names = (
  "lookup",   "access",   "create", "mkdir",   "truncate",
  "getattrs", "setattrs", "link",   "opendir", "readdir",
  "closedir", "open",     "read",   "write",   "close",
  "readlink", "symlink",  "rename", "unlink",  "mknode",
  "static_fsinfo", "dynamic_fsinfo", "rcp", "Init",
  "get_stats", "lock",    "changelock", "unlock",
  "BuildExportCtxt", "InitClientCtxt", "GetClientCtxt",
  "lookupPath", "lookupJunction",
  "ioctl", "test_access", "rmdir", "CleanObjRes","open_by_name", "open_by_fileid",
  "ListXAttrs", "GetXAttrValue", "SetXAttrValue", "close_by_fileid",
  "setattr_access", "merge_attrs", "rename_access", "unlink_access",
  "link_access", "create_access" 
 );


#### options ####

# indicates which layers' stats we want
my $select_tag = "ALL";
# the number of lines for each stat
my $tail_flags = "-19";

# Is the configuration file to be parsed in full ?
my $full_flag = 0 ;

### subroutines ####

sub usage
{
  my ( $fname ) = @_ ;
  print "\n";
  print "usage:   $fname  [-f] [-A] [-t <tag_expr>]  <file>\n" ;
  print "\n";
  print "\t<file>: the file to extract stats from\n" ;
  print "\t-f: 'tail -f' mode. If not specified, display the last stat.\n" ;
  print "\t-A: 'Whole file' mode. Analyze the whole file.\n" ;
  print "\t-t <tag_expr>: display only stats for tags that match <tag_expr>\n" ;
  print "\n";
}

sub arrondi
{
  my ( $float ) = @_ ;
  return sprintf( "%d", $float );
}

sub size_to_human
{
  my ( $size ) = @_ ;
  
  my $ret_str = "";
  
  if ( $size < 1024 )
  {
    return "$size B";
  }
  
  $size = ( $size / 1024.0 );
  
  if ( $size < 1024 )
  {
    $ret_str = sprintf( "%.2f kB", $size );
    return $ret_str;
  }
  
  $size = $size / 1024.0;
  
  if ( $size < 1024 )
  {
    $ret_str = sprintf( "%.2f MB", $size );
    return $ret_str;
  }
  
  $size = $size / 1024.0;
  
  if ( $size < 1024 )
  {
    $ret_str = sprintf( "%.2f GB", $size );
    return $ret_str;
  }
        
  $ret_str = sprintf( "%.2f TB", $size );
  return $ret_str;
}


#### analyse de la ligne de commandes ####

if ( ! getopts('hfAt:') )
{
  usage( basename( $0 ) );
  exit(1);  
}

if ( defined( $Getopt::Std::opt_h ) ) { usage( basename( $0 ) ); exit( 0 ); }
if ( defined( $Getopt::Std::opt_f ) ) { $tail_flags = "-f"; }
if ( defined( $Getopt::Std::opt_t ) ) { $select_tag = $Getopt::Std::opt_t; }
if ( defined( $Getopt::Std::opt_A ) ) { $full_flag = 1 ; }


if ( defined($ARGV[0]) )
{
  my $file = $ARGV[0];
  if( $full_flag == 1 ) 
   {
      open STATS, $file or die "Can't open $file" ;
   }
  else
   {
      open STATS, "$TAIL $tail_flags $file|" or die "Can't exec \"$TAIL $tail_flags\"";
   }
}
else
{
  usage(basename( $0 ));
  exit(1);
}


##### parcours du fichier #####

while (my $ligne=<STATS>)
{

  chomp($ligne);
  
  if ( $ligne =~ m/^([^,]+)\s*,\s*([^,]+)\s*,\s*([^;]+)\s*;(.*)$/ )
  {
    
    my $tag   = $1;
    my $epoch = $2;
    my $date  = $3;
    my $reste = $4;
    
    # if tag doesn't match
    next if ( !( $select_tag eq "ALL" ) && !( $tag =~ m/$select_tag/ ) );
    
    print "\n--------- $date: $tag ----------------\n\n";
    
    if ( $tag eq "NFS_SERVER_GENERAL" )    
    {
      next if ( ! ( $reste =~ m/^([^,]+),(.*)/ ) );  # go to next line
      
      my $epoch_start = $1;
      my $date_start = $2;
      
      my $uptime = $epoch - $epoch_start;
      
      my $uptime_day =  arrondi( $uptime / (3600*24) );
      $uptime = $uptime - (3600*24*$uptime_day);
      my $uptime_hour =arrondi( $uptime / 3600 );
      $uptime = $uptime - (3600*$uptime_hour);
      my $uptime_min = arrondi( $uptime / 60 );
      $uptime = $uptime - (60*$uptime_min);
      my $uptime_sec = $uptime ;      
      
      print "   Server is up since : $date_start\n";
      print "   Uptime : $uptime_day d, $uptime_hour h, $uptime_min min, $uptime_sec s.\n\n";      
      
    }    
    elsif ( $tag eq "CACHE_INODE_CALLS" )    
    {
      next if ( ! ( $reste =~ m/^([^,]+),([^,]+),([^|]+)(.*)/ ) );  # go to next line
      
      my $total_calls = $1;
      my $total_gc_lru_total = $2;
      my $total_gc_lru_active = $3;
      $reste = $4;
      
      print "Total calls : $total_calls\n\n";
      
      my $fn_index = 0;
      
      while (  $reste =~ m/\|([^,]+),([^,]+),([^,]+),([^|]+)(.*)/ )
      {
        my $nb_appels = $1;
        my $nb_succes = $2;
        my $nb_retryable = $3;
        my $nb_unrecov = $4;
        
        $reste = $5;
        
        if ( $fn_index == 0 )
        {
          # print header the first time
          printf( "%20s | %10s | %10s | %10s | %10s\n", "FUNCTION", "NB_CALLS", "OK", "RETRYABLE", "UNRECOV." );
        }
        
        printf( "%20s | %10d | %10d | %10d | %10d\n", $cache_inode_fn_names[$fn_index],
                $nb_appels, $nb_succes, $nb_retryable, $nb_unrecov );        
        
        $fn_index ++;        
      
      }
    }
    elsif ( ( $tag eq "CACHE_INODE_HASH" ) || ( $tag eq "DUP_REQ_HASH" ) || ( $tag eq "UIDMAP_HASH" ) || ( $tag eq "IP_NAME_HASH" ) ||
	    ( $tag eq "UNAMEMAP_HASH" )    || ( $tag eq "GIDMAP_HASH" )  || ( $tag eq "GNAMEMAP_HASH" ) )
    {
      next if ( ! ( $reste =~ m/^([^,]+),([^,]+),([^,]+),([^|]+)(.*)/ ) );  # go to next line
      
      my $nb_entries = $1;
      my $min_rbt = $2;
      my $max_rbt = $3;
      my $avg_rbt = $4;
      $reste = $5;
      
      print "Nb entries : $nb_entries\n";
      print "RB-Trees : min = $min_rbt, max = $max_rbt, avg = $avg_rbt\n\n";
      
            
      my $fn_index = 0;
      
      while (  $reste =~ m/\|([^,]+),([^,]+),([^|]+)(.*)/ )
      {
        my $nb_ok = $1;
        my $nb_notfound = $2;
        my $nb_err = $3;
        $reste = $4;
        
        my $pct_hit = 0.0;
        
        if ( $nb_ok + $nb_notfound > 0 )
        {
          $pct_hit = 100.0 * ( $nb_ok / ( $nb_ok + $nb_notfound ) );
        }        
        
        if ( $fn_index == 0 )
        {
          # print header the first time
          printf( "%20s | %10s | %10s | %7s | %8s\n", "FUNCTION", "OK", "NOT_FOUND", "ERRORS", "HITS" );
        }
        
        printf( "%20s | %10d | %10d | %7d | %7.2f%%\n", $cache_inode_hash_fn_names[$fn_index],
                $nb_ok, $nb_notfound, $nb_err, $pct_hit );
        
        $fn_index ++;        
      
      }    
    
    }
    elsif ( $tag eq "FSAL_CALLS" )
    {
      next if ( ! ( $reste =~ m/^([^|]+)(.*)/ ) );  # go to next line
      
      my $total_calls = $1;
      $reste = $2;
      
      print "Total calls : $total_calls\n\n";
      
      my $fn_index = 0;
      
      while (  $reste =~ m/\|([^,]+),([^,]+),([^,]+),([^|]+)(.*)/ )
      {
        my $nb_appels = $1;
        my $nb_succes = $2;
        my $nb_retryable = $3;
        my $nb_unrecov = $4;
        
        $reste = $5;
        
        if ( $fn_index == 0 )
        {
          # print header the first time
          printf( "%20s | %10s | %10s | %10s | %10s\n", "FUNCTION", "NB_CALLS", "OK", "RETRYABLE", "UNRECOV." );
        }
        
        printf( "%20s | %10d | %10d | %10d | %10d\n", $fsal_fn_names[$fn_index],
                $nb_appels, $nb_succes, $nb_retryable, $nb_unrecov );
        
        $fn_index ++;        
      
      }
    }
    elsif ( $tag eq "NFS/MOUNT STATISTICS" )
    {
      next if ( ! ( $reste =~ m/^([^,]+),([^,]+),([^,]+)\|([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)\|([^,]+),([^,]+),([^,]+),([^,]+)/ ) );  # go to next line
      
      my $tot_req = $1;
      my $nb_udp = $2;
      my $nb_tcp = $3;
      my $nb_mnt1 = $4;
      my $nb_mnt3 = $5;
      my $nb_nfs2 = $6;
      my $nb_nfs3 = $7;
      my $nb_nfs4 = $8;
      my $nb_total_pending_request = $9;
      my $nb_min_pending_request = $10 ;
      my $nb_max_pending_request = $11 ;
      my $nb_average_pending_request = $12 ;
      
      print "\tTotal requests : $tot_req\n";
      print "\tudp : $nb_udp\n";
      print "\ttcp : $nb_tcp\n";
      print "\tmnt1 : $nb_mnt1\n";
      print "\tmnt3 : $nb_mnt3\n";
      print "\tnfs2 : $nb_nfs2\n";
      print "\tnfs3 : $nb_nfs3\n";
      print "\tnfs4 : $nb_nfs4\n";

      printf "\n\tPending Requests (for all workers): $nb_total_pending_request\n" ;
      printf "\tminimum pending request: $nb_min_pending_request \n" ;
      printf "\tmaximum pending request: $nb_max_pending_request \n" ;
      printf "\taverage pending request: $nb_average_pending_request \n" ;
    
    }
    elsif ( $tag =~ m/MNT V. REQUEST/ )
    {
      next if ( ! ( $reste =~ m/^([^|]+)(.*)/ ) );  # go to next line
      
      my $total = $1;
      $reste = $2;
      
      print "Nb requests : $total\n\n";      
            
      my $fn_index = 0;
      
      while (  $reste =~ m/\|([^,]+),([^,]+),([^|]+)(.*)/ )
      {
        my $nb_tot = $1;
        my $nb_ok = $2;
        my $nb_dropp = $3;
        $reste = $4;
        
        if ( $fn_index == 0 )
        {
          # print header the first time
          printf( "%20s | %10s | %10s | %10s\n", "FUNCTION", "NB_CALLS", "OK", "DROPPED");
        }
        
        printf( "%20s | %10d | %10d | %10d \n", $mount_fn_names[$fn_index],
                $nb_tot, $nb_ok, $nb_dropp );
        
        $fn_index ++;
      
      }
    }
    elsif ( $tag =~ m/RQUOTA V. REQUEST/ )
    {
      next if ( ! ( $reste =~ m/^([^|]+)(.*)/ ) );  # go to next line
      
      my $total = $1;
      $reste = $2;
      
      print "Nb requests : $total\n\n";      
            
      my $fn_index = 0;
      
      while (  $reste =~ m/\|([^,]+),([^,]+),([^|]+)(.*)/ )
      {
        my $nb_tot = $1;
        my $nb_ok = $2;
        my $nb_dropp = $3;
        $reste = $4;
        
        if ( $fn_index == 0 )
        {
          # print header the first time
          printf( "%20s | %10s | %10s | %10s\n", "FUNCTION", "NB_CALLS", "OK", "DROPPED");
        }
        
        printf( "%20s | %10d | %10d | %10d \n", $rquota_fn_names[$fn_index],
                $nb_tot, $nb_ok, $nb_dropp );
        
        $fn_index ++;
      
      }
    }
   elsif ( $tag =~ m/NLM V. REQUEST/ )
    {
      next if ( ! ( $reste =~ m/^([^|]+)(.*)/ ) );  # go to next line
      
      my $total = $1;
      $reste = $2;
      
      print "Nb requests : $total\n\n";      
            
      my $fn_index = 0;
      
      while (  $reste =~ m/\|([^,]+),([^,]+),([^|]+)(.*)/ )
      {
        my $nb_tot = $1;
        my $nb_ok = $2;
        my $nb_dropp = $3;
        $reste = $4;
        
        if ( $fn_index == 0 )
        {
          # print header the first time
          printf( "%20s | %10s | %10s | %10s\n", "FUNCTION", "NB_CALLS", "OK", "DROPPED");
        }
        
        printf( "%20s | %10d | %10d | %10d \n", $nlm_fn_names[$fn_index],
                $nb_tot, $nb_ok, $nb_dropp );
        
        $fn_index ++;
      
      }

    }
    elsif ( $tag =~ m/NFS V(.) REQUEST/ )
    {
      my $vers = $1;
      
      next if ( ! ( $reste =~ m/^([^|]+)(.*)/ ) );  # go to next line
      
      my $total = $1;
      $reste = $2;
      
      print "Nb requests : $total\n\n";      
            
      my $fn_index = 0;
      
      while (  $reste =~ m/\|([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^|]+)(.*)/ )
      {
        my $nb_tot = $1;
        my $nb_ok = $2;
        my $nb_dropp = $3;

        my $tot_latency;
        my $avg_latency;
        my $min_latency;
        my $max_latency;

        if ( $vers == 3 )
        {
          $tot_latency = $4;
          $avg_latency = $5;
          $min_latency = $6;
          $max_latency = $7;
          $reste = $8;
        }
        else
        {
          $reste = $4;
        }
        
        if ( $fn_index == 0 )
        {
          # print header the first time
          if ( $vers == 3 )
          {
            printf( "%20s | %10s | %10s | %10s | %10s | %10s | %10s | %10s\n", "FUNCTION", "NB_CALLS", "OK", "DROPPED",
                    "TOT_LAT", "AVG", "MIN", "MAX" );
          }
          else
          {
            printf( "%20s | %10s | %10s | %10s\n", "FUNCTION", "NB_CALLS", "OK", "DROPPED" );
          }
        }
        
        if ( $vers == 2 )
        {
          printf( "%20s | %10d | %10d | %10d \n", $nfs2_fn_names[$fn_index],
                   $nb_tot, $nb_ok, $nb_dropp );
        }
        elsif ( $vers == 3 )
        {
          printf( "%20s | %10d | %10d | %10d | %10d | %10d | %10d | %10d \n", $nfs3_fn_names[$fn_index],
                   $nb_tot, $nb_ok, $nb_dropp, $tot_latency, $avg_latency, $min_latency, $max_latency );
        }
        elsif ( $vers == 4 )
        {
          printf( "%20s | %10d | %10d | %10d \n", $nfs4_fn_names[$fn_index],
                   $nb_tot, $nb_ok, $nb_dropp );
        }
        
        $fn_index ++;
      
      }
    
    }
    elsif ( $tag =~ m/NFS V4\.(.) OPERATIONS/ )
    {
      my $minorversion = $1;
      
      next if ( ! ( $reste =~ m/^([^|]+)(.*)/ ) );  # go to next line

      my $total = $1;
      $reste = $2;
      
      print "Nb requests : $total\n\n";      
 
      my $fn_index = 0;
      
      while (  $reste =~ m/\|([^,]+),([^,]+),([^|]+)(.*)/ )
      {
        my $nb_tot = $1;
        my $nb_ok = $2;
        my $nb_failed = $3;
        $reste = $4;
        
        if ( $fn_index == 0 )
        {
          # print header the first time
          printf( "%20s | %10s | %10s | %10s\n", "FUNCTION", "NB_CALLS", "OK", "failed");
        }
      
       if( $minorversion == 0 ) 
        {
          printf( "%20s | %10d | %10d | %10d \n", $nfs40_op_names[$fn_index],
                  $nb_tot, $nb_ok, $nb_failed );
        }
       elsif( $minorversion == 1 )
        {
          printf( "%20s | %10d | %10d | %10d \n", $nfs41_op_names[$fn_index],
                  $nb_tot, $nb_ok, $nb_failed );
        }

        $fn_index ++;
      
      }
    
    }


    elsif ( $tag eq "BUDDY_MEMORY" )
    {
      next if ( ! ( $reste =~ m/^([^,]+),([^,]+),([^,]+)\|([^,]+),([^,]+),([^,]+)\|([^,]+),([^,]+),([^,]+),([^,]+)/ ) );  # go to next line
      
      my $tot_mem = $1;
      my $std_mem = $2;
      my $extra_mem = $3;
      
      my $std_used = $4;
      my $std_avg = $5;
      my $std_max = $6;
      
      my $nb_pages_tot = $7;
      my $nb_pages_used = $8;
      my $nb_pages_avg = $9;
      my $nb_pages_max = $10;
      
      
      printf( "\tTotal preallocated memory :             %10s\n", size_to_human($tot_mem)  );
      printf( "\t   Preallocated memory for std pages:   %10s\n", size_to_human($std_mem)  );
      printf( "\t   Preallocated memory for extra pages: %10s\n", size_to_human($extra_mem));
      
      print   "\n\tStd space detail:\n" ;
      printf( "\tPreallocated : %10s\n", size_to_human($std_mem) );
      printf( "\tUsed         : %10s (%.2f%%)\n", size_to_human($std_used), 100.0 * $std_used / $std_mem );
      printf( "\tThread avg   : %10s\n", size_to_human($std_avg ));
      printf( "\tThread max   : %10s\n", size_to_human($std_max ));

      print   "\n\tStd pages detail:\n" ;
      printf( "\tPreallocated : %5u\n", $nb_pages_tot );
      printf( "\tUsed         : %5u\n", $nb_pages_used );
      printf( "\tThread avg   : %5u\n", $nb_pages_avg );
      printf( "\tThread max   : %5u\n", $nb_pages_max );
      
    }

   
   
  }
  elsif ( $ligne =~ m/^END,/ )
  {
    print "\n* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\n";
  }
  elsif ( $ligne ne "" )
  {
    print "Unrecognized line \"$ligne\"\n" ;
  }
  


}
