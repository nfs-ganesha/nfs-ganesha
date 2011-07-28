#!/bin/bash

### PLACES ###
Here=$PWD
Prog=$(basename $0)
thisDir=$(basename $PWD)
topDir=$(dirname $0)/..
statDir=${topDir}/STATUS
webDir=${topDir}/../www/admin

### VALID VALUES ###
# Anyone who might be doing editing is an Owner...
Owners="benlevenson ccr_linuxsymposium eugeneteo gozen jaboutboul jfeeney jpoelstra jwlockhart ldimaggio stepan_kasal terryj none N/A"

# Every paper has a known directory name...
Papers="ben-yehuda bergmann bligh branco briglia brown_1 brown_2 brown_z cagney chen crouse deniel forbes french gorman gough goyal harper hiramatsu holtmann ke keniston kivity komkoff kroah-hartman lahey lameter linkov litke lunev lutterkort martin mason mathur melo menage mirtchovski muralidhar nagar nahari nakajima opdenacker padioleau pallipadi perez-gonzalez phillips poettering potzl rostedt russell sato schlesinger siddha singh sipek szeideman ueda vangend whitehouse wilder wu yoshioka zhang zhu zijlstra"
# dropped or presentation-only papers...
Dropped="wong corbet thompson marinas hohndel gross welte wilson"

# Possible states
States="missing idle busy complete DROPPED presentation"

### GLOBAL VARIABLES / OPTIONS ###
owner=''
ready=''
state=''
graphics=''
todo=''
comments=''
paper=''
export MakeNew=false
SkipAdmin=false
AdminOnly=false

###### HELP FUNCTION ##########
function doHelp {
    echo
    echo Usage:
    echo "  $Prog -p paper -o owner -s state -r readiness -g graphics -t todo -c comment"
    echo Or:
    echo "  $Prog -h for this help"
    echo
    cat <<EOF
A paper name is required; anything else will be read from the existing Status file.
Thus you could edit the Status file in your favorite editor, and just do
   $Prog paper
instead, if so inclined.  Arguments are as follows:

-a Do not update the administrative webpage
-A Only update the administrative webpage (no paper name required)

Owner: 108 username of editor who will be working on it
State: One of:
       missing:  paper not yet received
       idle:     not actively working on it
       busy:     actively working on it
       complete: finished working on it
Readiness:  numeric assessment (1 for trainwreck, 10 for perfection) 
            of readiness for publication.  0 means not yet assessed.
Graphics:   Freeform one-liner to describe any problems with figures.
Todo:       Freeform one-liner to describe what needs to be done.
Comment:    Freeform one-liner for additional comments, please be polite.

Note: Any values that you do not provide are preserved from the
existing Status file.

Valid States:
   $States

Valid Owners:
   $Owners

Valid Papers:
   $Papers

EOF
}

#### OTHER FUNCTIONS #########
function svnUpdateStatusDir {
    if ! $MakeNew ; then
	echo "Updating Status directory to latest rev..."
	cd $statDir
	svn update
	cd $Here
	cd $topDir
	svn update Data.sh
	cd $Here
	echo "Done."
    fi
    if ! $SkipAdmin ; then
        cd $webDir
        svn update
        cd $Here
    fi
}
function updateSVNItems {
    checkIn=''
    ciMsg=''
    doCheckin=false
    if ! $AdminOnly ; then
        if ! $MakeNew ; then
           checkIn="$checkIn ${statDir}/${paper}"
	   ciMsg=$(echo "$paper $ready // $owner // $state // $lastUpdate") 
           doCheckin=true
        fi
    fi
    if ! $SkipAdmin ; then
	checkIn="$checkIn ${webDir}/index.html"
        if [ -z "$ciMsg" ] ; then
           ciMsg="Update for $paper"
        fi
	# svn ci -m "Update for $paper" index.html
        doCheckin=true
    fi
    cd $Here
    if $doCheckin ; then
       echo "Committing status updates to SVN..."
       echo svn ci -m \""${ciMsg}"\" $checkIn
       svn ci -m "${ciMsg}" $checkIn
    fi
}
function isOwner {
    local i=''
    candidate="$1"
    for i in $Owners ; do
	if [[ "$candidate" == "$i" ]]; then
	    return 0
	fi
    done
    return 1
}
function isPaper {
    local i=''
    candidate="$1"
    for i in $Papers ; do
	if [[ "$candidate" == "$i" ]]; then
	    return 0
	fi
    done
    return 1
}
function isState {
    local i=''
    candidate="$1"
    for i in $States ; do
	if [[ "$candidate" == "$i" ]]; then
	    return 0
	fi
    done
    return 1
}
function getPageCount {
    p=$1
    for f in $topDir/$p/${p}-proc.pdf $topDir/$p/${p}.pdf ; do
	if [ -f $f ] ; then
	    pdfinfo $f 2>&1 | grep '^Pages:' | awk '{ print $2 }'
	    return
	fi
    done
    if [ -f $topDir/STATUS/$p ] ; then
	grep -i '^page' $topDir/STATUS/$p | awk '{ print $2 }'
	return
    fi
    echo 0
}
function writeStatusFile {
    for i in paper owner readiness state pages graphics todo comments lastUpdate ; do
	eval echo ${i}: \$${i}
    done > "$1"
}
function writeAdminPage {
    local ProcPgDesc='Pages in last-built Proceedings.pdf'
    # Note: 7 means ready, but could use a final proofing.
    local ready7=0
    local ready8=0
    local ready9=0
    local ready10=0
    local readyOK=0
    local paperCnt=0
    local readyNot=0

    # Announce what we're up to and cd appropriately...
    echo "Writing Admin Page..."
    cd $Here

    # Generate a simple TODO summary based on readiness stats
    ready7=$(cat $topDir/STATUS/* | grep -c 'readiness: *7')
    ready8=$(cat $topDir/STATUS/* | grep -c 'readiness: *8')
    ready9=$(cat $topDir/STATUS/* | grep -c 'readiness: *9')
    ready10=$(cat $topDir/STATUS/* | grep -c 'readiness: *10')
    readyOK=$(( ${ready7} + ${ready8} + ${ready9} + ${ready10} ))
    paperCnt=$(grep -c RECEIVED= $topDir/Data.sh)
    readyNot=$(( $paperCnt - $readyOK ))

    # Set output file
    webFile=$webDir/index.html

    # Get page count estimate for complete Proceedings
    topPC='(unknown)'
    if [ -f $topDir/Proceedings.pdf ] ; then
	topPC=$(pdfinfo $topDir/Proceedings.pdf | grep '^Pages:' | awk '{ print $2 }')
    else
	topPC=$(grep "$ProcPgDesc" $webFile | sed -e 's/<p>//' | awk '{ print $1 }')
    fi

    # Now create the admin web page
    cat > $webFile <<EOF
<html>
<head>
<title>OLS2007 Status</title>
</head>
<body>
<h2>OLS2007 Proceedings Status</h2>
<p>$(grep -c RECEIVED=1 $topDir/Data.sh) Papers Received</p>
<p>$(grep -c RECEIVED=0 $topDir/Data.sh) Papers Missing</p>
<p>$topPC $ProcPgDesc</p>
<hr>
<p>${readyOK} Papers Edited: readiness of 7, 8, 9, or 10</p>
<p>${readyNot} Papers To Be Edited</p>
<p>${ready7} Papers could use a final proofing review</p>
<hr>
<table width="100%" border="1">
<tr><td><b>Paper<b></td><td><b>Owner<b></td><td><b>Pages<b></td>
<td><b>Readiness<b></td><td><b>State<b></td><td><b>Graphics<b></td>
<td><b>ToDo<b></td><td><b>Comments<b></td><td><b>Updated<b></td></tr>
EOF
    for i in $Papers ; do
	fp=$statDir/$i
	# owner
	o=$(grep '^owner:' $fp | cut -d' ' -f2-) 
	# ready
	r=$(grep '^readiness:' $fp | cut -d' ' -f2-) 
	# state
	s=$(grep '^state:' $fp | cut -d' ' -f2-)
	# graphics
	g=$(grep '^graphics:' $fp | cut -d' ' -f2-)
	# todo
	t=$(grep '^todo:' $fp | cut -d' ' -f2-)
	# comments
	c=$(grep '^comments:' $fp | cut -d' ' -f2-)
	# lastUpdate
	u=$(grep '^lastUpdate:' $fp | cut -d' ' -f2-)
	pgs=$(getPageCount $i)
	echo -n '<tr>'
	echo -n '<td>'${i}'</td>'
	echo -n '<td>'${o}'</td>'
	echo -n '<td>'${pgs}'</td>'
	echo -n '<td>'${r}'</td>'
	echo -n '<td>'${s}'</td>'
	echo -n '<td>'${g}'</td>'
	echo -n '<td>'${t}'</td>'
	echo -n '<td>'${c}'</td>'
	echo -n '<td>'${u}'</td>'
	echo '</tr>'
    done >> $webFile
    cat >> $webFile <<EOF2
</table>
<hr>
<p>This page last generated, local time: $(date)</p>
</body>
</html>
EOF2
echo "Done."
}

##### OPTIONS ########

while true ; do
    [ -z "$1" ] && break;
    case "$1" in
	-o)
	    # owner
	    shift ; owner="$1"
	    ;;
	-r)
	    # readiness
	    shift; ready="$1"
	    ;;
	-s)
	    # state
	    shift; state="$1"
	    ;;
	-g) 
	    # graphics
	    shift; graphics="$1"
	    ;;
	-t)
	    # to-do
	    shift; todo="$1"
	    ;;
	-c) 
	    # comments
	    shift; comments="${1}"
	    ;;
	-p)
	    # paper
	    shift; paper="$1"
	    ;;
	-h)
	    doHelp ; exit 0
	    ;;
	--help)
	    doHelp ; exit 0
	    ;;
	-a)
	    SkipAdmin=true ;;
	-A) 
	    AdminOnly=true ;;
	-n)
	    # NEW, for setup-time only
	    MakeNew=true
	    ;;
	-*)
	    echo
	    echo "Unknown argument: $1"
	    echo
	    doHelp ; exit 1
	    ;;
	*)
	    # paper?
	    paper="$1"
	    ;;
    esac
    shift
done

if ! $AdminOnly ; then
    if [ -z "$paper" ] ; then
	doHelp
	if isPaper $thisDir ; then 
	    echo
	    echo 'Did you mean to type the following?'
	    echo "  $Prog -p $thisDir"
	    echo
	fi
	exit 1
    fi
    if ! isPaper "$paper" ; then
	echo "ERROR: unknown paper: $paper"
	exit 1
    fi
fi

# Work with the latest status files
svnUpdateStatusDir

if ! $AdminOnly ; then
    fpaper=$statDir/$paper

    if [ ! -f "$fpaper" ] ; then
	if $MakeNew; then
	touch $fpaper
	else
	    echo "ERROR: cannot locate status file: $fpaper"
	    exit 1
	fi
    fi

    [ -z "$owner" ]    && owner=$(grep '^owner:' $fpaper | cut -d' ' -f2-) 
    [ -z "$ready" ]    && ready=$(grep '^readiness:' $fpaper | cut -d' ' -f2-) 
    [ -z "$state" ]    && state=$(grep '^state:' $fpaper | cut -d' ' -f2-)
    [ -z "$graphics" ] && graphics=$(grep '^graphics:' $fpaper | cut -d' ' -f2-)
    [ -z "$todo" ]     && todo=$(grep '^todo:' $fpaper | cut -d' ' -f2-)
    [ -z "$comments" ] && comments=$(grep '^comments:' $fpaper | cut -d' ' -f2-)


    # Default values if there were none in the file...
    [ -z "$owner" ]    && owner='none'
    [ -z "$ready" ]    && ready=0
    [ -z "$state" ]    && state='missing'
    [ -z "$graphics" ] && graphics='unknown'
    [ -z "$todo" ]     && todo='everything'
    [ -z "$comments" ] && comments='none'

    if ! isOwner "$owner" ; then
	echo "ERROR: unknown owner: $owner"
	echo "Check the help if you have not made a typo."
	exit 1
    fi
    if ! isState "$state" ; then
	echo "ERROR: unknown state: $state"
	echo "Valid states: $States"
	exit 1
    fi
    lastUpdate=$(TZ=UTC date)
    readiness=$ready
    pages=$(getPageCount $paper)
    [ -z "$pages" ] && pages=0
    if [ $pages -gt 2 ]; then
	if [ $state = "missing" ]; then
	    state='idle'
	fi
    fi
fi

if ! $AdminOnly ; then
    writeStatusFile "$fpaper"
    echo
    echo "New Status File:"
    cat $fpaper
    echo
fi

if ! $MakeNew ; then
    if ! $SkipAdmin ; then
	writeAdminPage
    fi
    updateSVNItems
fi

