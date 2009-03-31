#!/bin/bash

# define functions to be anything that needs doing across the papers...
function do_prep {
  echo
  echo 'OLS 2007: SVN Table of Contents'
  echo 
  echo 'Draft: DO NOT REDISTRIBUTE UNTIL AFTER THE CONFERENCE'
  echo 
  printf "%-20s %-20s %-40s\n" "Directory" "Author" "Title"
  printf "%-20s %-20s %-40s\n" "=========" "======" "====="
}
function do_work {
   dirName=$(echo "$AUTHOR" | awk '{ print $NF }' | tr '[:upper:]' '[:lower:]')
   printf "%-20s %-20s %-40s\n" "$dirName" "$AUTHOR" "$TITLE"
}
function do_wrapup {
    echo
}

source $(dirname $0)/Data.sh

