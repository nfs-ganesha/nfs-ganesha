#!/bin/sh

CURDIR=$(dirname $(readlink -m $0))
HOOKDIR=$(git rev-parse --show-toplevel)/.git/hooks

cp $CURDIR/pre-commit $HOOKDIR/
chmod +x  $HOOKDIR/pre-commit

cp $CURDIR/commit-msg $HOOKDIR
chmod +x  $HOOKDIR/commit-msg

