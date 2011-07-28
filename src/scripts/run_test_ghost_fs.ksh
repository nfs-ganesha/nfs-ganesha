#!/bin/ksh 

# OS
OS=`archi -M`

# output directory.
OUTDIR=../../dump
# input file
GHOSTFS_CONF=../../share/conf/ghostfs.conf

# parser test bin
PARSER_TEST=../../bin/$OS/dbg/test_libghostfs_parse
# ghostfs test bin
GHOSTFS_TEST=../../bin/$OS/dbg/test_libghostfs


case $1 in
    1)
        # 1 - tests the parser
        $PARSER_TEST  $GHOSTFS_CONF $OUTDIR/ghostfs.dump 2>&1
        diff -w $GHOSTFS_CONF $OUTDIR/ghostfs.dump | wc |awk '{ print "-"$1"-" }';;
    2)
        # 2 - tests the ghost_fs by doing a ls
        $GHOSTFS_TEST   -ls $GHOSTFS_CONF "$OUTDIR/out1" "$OUTDIR/out2" 2>&1
        diff "$OUTDIR/out1" "$OUTDIR/out2" | wc |awk '{ print "-"$1"-" }';;
    3)  # 3 - tests the ghost_fs access
        $GHOSTFS_TEST   -acces $GHOSTFS_CONF "/share/bin/sherpa_prret.S" $2 $3 ;;
    *) echo "Invalid test command"
       exit -1 ;;  # invalid command
esac
