#!/bin/bash

export WEBSITE=linuxsymposium
export YEAR=2007

# define functions to be anything that needs doing across the papers...
function do_prep {
  echo did prep
}

function do_work {
    paper="${PAPER_ID}"
    author="${AUTHOR}" 
    bio="${BIO_ID}" 
    key="${CONTENT_KEY}" 
    title="${TITLE}" 

    Dir=$(echo $author | tr '[:upper:]' '[:lower:]' | awk '{ print $NF }')
    echo Dir is $Dir
    echo Paper is $paper
    echo Author is $author
    echo bio is $bio and key is $key
    echo Title is "$title"
    echo " "
    Start=$PWD
    MakeAdd="${Dir}/${Dir}-abstract.tex"
    if [ ! -d $Dir ] ; then mkdir $Dir || die "cannot mkdir $Dir" ; fi
    cd $Dir || die "cannot cd $Dir"

    if [ ! -r ${Dir}-abstract.tex ] ; then
	### CREATE ABSTRACT (pull from $WEBSITE if available)
	if [ $key -ne 0 ] ; then
            # http://www.linuxsymposium.org/2007/view_abstract.php?content_key=43
	    links -dump 'http://www.'${WEBSITE}'.org/'${YEAR}'/view_abstract.php?content_key='$key > ${Dir}-abstract.tex
	else
	    echo " " > ${Dir}-abstract.tex
	fi
    fi
    
    if [ ! -r Makefile.inc ] ; then
	### CREATE Makefile.inc
	cat > Makefile.inc <<EOF
PAPERS += ${Dir}/${Dir}.dvi

## Add any additional .tex or .eps files below:
${Dir}/${Dir}.dvi ${Dir}/${Dir}-proc.dvi: \\
	${Dir}/${Dir}.tex \\
	${Dir}/${Dir}-abstract.tex

${Dir}/${Dir}-%.pdf: ${Dir}/${Dir}-%.eps
	epstopdf $<

## If you are generating eps from dia, uncomment:
# ${Dir}/${Dir}-%.eps: ${Dir}/${Dir}-%.dia
#	dia --export=$@ -t eps-builtin $^

EOF
    fi
    if [ ! -r ${Dir}.tex ] ; then
	### CREATE BLANK PAPER
	## __TITLE__ __SUBTITLE__ __AUTHOR__ __ABSTRACT__
	echo 'title  : "'${title}'"'
	echo 'author : "'${author}'"'
	addMake=$(basename $MakeAdd)
	echo 'addMake: "'${addMake}'"'
	cat $Start/TEMPLATES/autoauthor.tex | \
	sed -e "s|__TITLE__|${title}|g" | \
	sed -e 's|__SUBTITLE__| |g' | \
	sed -e "s|__AUTHOR__|${author}|g" | \
	sed -e 's|__ABSTRACT__|'${addMake}'|g' > ${Dir}.tex
    fi

    cd $Start
}


function do_wrapup {
  echo did wrapup
}

source ./Data.sh


# PAPER_ID=1
# TITLE="Enabling Docking Station Support for the Linux Kernel is Harder Than You Would Think"
# BIO_ID="125"
# CONTENT_KEY="95"
# AUTHOR="Kristen Carlson Accardi"
# RECEIVED=0
# 
# export PAPER_ID TITLE BIO_ID CONTENT_KEY AUTHOR RECEIVED
# 
# do_work

# do_wrapup

