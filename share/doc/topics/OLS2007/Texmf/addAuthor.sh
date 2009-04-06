#!/bin/bash

# This script should be called from the top OLS/GCC directory
# (eg the one that contains the Texmf directory)

# gccsummit or linuxsymposium ...
export WEBSITE=linuxsymposium
export YEAR=2007

# Handle fatal errors
function die {
    echo $*
    exit 1
}

# Prepare new paper/author
function do_paper {
    paper="$1"
    author="$2" 
    bio="$3" 
    key="$4" 
    title="$5" 

    Dir=$(echo "$author" | tr '[:upper:]' '[:lower:]' | awk '{ print $NF }')
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

### Example usage...
## PAPER_ID=1
## AUTHOR="Andrey Belevantsev"
## BIO_ID=0
## CONTENT_KEY=11
## TITLE="Improving GCC instruction scheduling for Itanium"

## do_paper $PAPER_ID "$AUTHOR" $BIO_ID $CONTENT_KEY "$TITLE"
function do_help {
   echo "Usage: $0 PAPER AUTHOR BIO_ID CONTENT_KEY TITLE"
   echo "Paper = integer greater than last-used one for papers"
   echo "Author = full author name, quoted"
   echo "Bio_ID = number from conference website or 0 for not available"
   echo "Content_Key = number for abstract from conference website, 0 for not available"
   echo "Title = title of paper, quoted"
}

if [ -z "$*" ] ; then
   do_help
   exit 0
fi
if [[ "$1" = *help* ]] ; then
   do_help
   exit 0
fi
if [[ "$1" = *-h* ]] ; then
   do_help
   exit 0
fi

do_paper ${@}

