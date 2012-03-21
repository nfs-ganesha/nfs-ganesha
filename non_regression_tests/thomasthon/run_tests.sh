#!/bin/sh

CURDIR=`dirname $0`
# include test framework
. $CURDIR/test_framework.inc

if [[ -r ./$CURDIR/test_variables.rc ]]  ; then
   .  ./$CURDIR/test_variables.rc
else
  echo "  /!\\ Please make test's configuration in  $CURDIR/test_variables.rc"
  exit 1 
fi

# The script uses "su", it has to be run as root
if [[ `id -u` != 0 ]] ; then
  echo "  /!\\ This script must be run as root"
  exit 1 
fi

########################## TEST HELPERS ##################

function space_used
{
    df -k $TEST_DIR | xargs | awk '{ print $(NF-3)}'
}

function write_small_file
{
    file_arg=$1
    dd if=/dev/zero "of=$file_arg" bs=1M count=10 || error "ERROR writing 1OMB to $file_arg"
}

function write_small_file_random
{
    file_arg=$1
    dd if=/dev/urandom "of=$file_arg" bs=1M count=10 || error "ERROR writing 1OMB to $file_arg"
}

function write_med_file_random
{
    file_arg=$1
    dd if=/dev/urandom "of=$file_arg" bs=10M count=10 || error "ERROR writing 1O0MB to $file_arg"
}

function append_file
{
    file_arg=$1
    dd if=/dev/zero "of=$file_arg" bs=1M count=10  conv=notrunc oflag=append || error "ERROR appending 1OMB to $file_arg"
}

function get_test_user
{
    # get the first user which is not in group 0 (root)
    # and who is allowed to login
    grep -v nologin /etc/passwd | grep -v ':0:' | head -1 | cut -d ':' -f 1
}

function do_as_user
{
    user_arg=$1
    action_arg=$2
    su - $user_arg -c "$action_arg"
}

function empty_client_cache
{
    [ "$DEBUG" = "1" ] && echo "emptying client cache (data+metadata)"
    echo 3 > /proc/sys/vm/drop_caches
}

function create_tree
{
    local ROOT=$1
    local DEPTH=$2
    local WIDTH=$3

    [ -n "$ROOT" ] || error "invalid arg to $0: $1"
    [ -n "$DEPTH" ] || error "invalid arg to $0: $2"
    [ -n "$WIDTH" ] || error "invalid arg to $0: $3"

    if (($DEPTH > 0)); then
        # create a subdir
        ((nb_file=$WIDTH - 1))
        [ "$DEBUG" = "1" ] && echo "mkdir $ROOT/subdir"
        mkdir $ROOT/subdir
        create_tree $ROOT/subdir $(($DEPTH - 1)) $WIDTH
    else
        nb_file=$WIDTH
    fi

    for f in $(seq 1 $nb_file); do
        [ "$DEBUG" = "1" ] && echo "touch $ROOT/file.$f"
        touch $ROOT/file.$f
    done
}

############################ TEST FUNCTIONS ############################

### test 1: check that a user can copy 444 file
function test1
{
    # create user directory
    dir="$TEST_DIR/dir.$USER.$$"
    file="$dir/test_file"

    mkdir $dir || error "error mkdir $dir"
    chown $USER $dir

    # create a file as user in this dir
    do_as_user $USER "dd if=/dev/zero of=$file bs=1M count=1" || "error writing $file"

    # set file 444
    chmod 444 "$file" || error "error chmod 444 $file"

    # copy the file as user
    do_as_user $USER "cp $file $file.copy" || "error copying $file to $file.copy"

    ls -l $file.copy || error "target file $file.copy not found"
    
    # clean test dir
    rm -rf $dir
}

### test 2: check recursive removal of large directories
function test2
{
    dir="$TEST_DIR/dir.$$"

    mkdir -p $dir

    echo "creating wide namespace..."
    create_tree $dir 2 10000
    empty_client_cache

    count=$(find $dir | wc -l)
    [ $count = 30003 ] || error "unexpected entry count in $dir"

    rm -r $dir || error "couldn't remove $dir"
}

### test3b : cthon04's basic tests
function test3b
{
   dir="$TEST_DIR/dir.$$"
   mkdir -p $dir

   cd $CTHON04_DIR/basic
   export NFSTESTDIR=$dir   

   timeout -s KILL 5m ./runtests || error "ERROR while running cthon04's basic tests"
}

### test3g : cthon04's general tests
function test3g
{
   dir="$TEST_DIR/dir.$$"
   mkdir -p $dir

   cd $CTHON04_DIR/general
   export NFSTESTDIR=$dir   

   timeout -s KILL 5m ./runtests || error "ERROR while running cthon04's general tests"
}

### test3s : cthon04's special tests
function test3s
{
   dir="$TEST_DIR/dir.$$"
   mkdir -p $dir

   cd $CTHON04_DIR/special
   export NFSTESTDIR=$dir   

   timeout -s KILL 5m ./runtests || error "ERROR while running cthon04's special tests"
}

### test3l : cthon04's lock tests
function test3l
{
   dir="$TEST_DIR/dir.$$"
   mkdir -p $dir

   cd $CTHON04_DIR/lock
   export NFSTESTDIR=$dir   

   timeout -s KILL 5m ./runtests || error "ERROR while running cthon04's lock tests"
}


# syntax: ONLY=2,3 ./run_test.sh [-j] <test_dir>

######################## DEFINE TEST LIST HERE ####################

run_test test1   "copy file with 444 mode"
run_test test2   "rm -rf of wide namespace"
run_test test3b  "cthon04's basic tests"
run_test test3g  "cthon04's general tests"
run_test test3s  "cthon04's special tests"
run_test test3l  "cthon04's lock tests"

# display test summary / generate outputs
test_finalize

