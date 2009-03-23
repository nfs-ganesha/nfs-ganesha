#!/bin/ksh 

CONFIG_PARSER=../../../bin/RHEL_4__i686/dbg/parse_file
GETHOST=/usr/local/sr/bin/cea_gethost 

function aglae_hdr
{
   local jour=`date +"%d"`
   local mois=`date +"%m"`
   local annee=`date +"%Y"`

   local hms=`date +"%T"`
   local host=`hostname`

   echo "$jour/$mois/$annee $hms : $host : Resolv-Client-NFS.ksh-$$ :$1"
}


CLIENT_LIST=`$CONFIG_PARSER $1  | grep Access | grep -v Access_ | grep -v CacheInode_Client::Use_Test_Access | cut -d '=' -f 2 | sed -e 's/,/ /g' | sort -u | grep -v \* | xargs`

for item in $CLIENT_LIST ; do 
        $GETHOST $item  1>/dev/null 2>/dev/null
        if [[ $? != 0 ]] ; then
                # Not a host, is this a network ? 
                if [[ -f /etc/networks ]] ; then
                        nbline=`grep $item /etc/networks | wc -l`
                else
                        nbline=0
                 fi

                if [[ $nbline == 0 ]] ; then
                        # Not a host or a network, is this a joker ? 
                        nbc=`echo $item | grep \* | wc -l`
                        if [[ $nbc == 0 ]] ; then
                                aglae_hdr "$item est inconnu"
                        fi
                fi
        fi

done

