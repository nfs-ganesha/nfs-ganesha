#!/bin/sh

CURDIR=$(dirname "$(readlink -m "$0")")
TOPDIR=$(git rev-parse --show-toplevel)
HOOKDIR=$TOPDIR/.git/hooks

# Link checkpatch script configuration file to top level working
# directory.
ln -sf ./src/scripts/checkpatch.conf "$TOPDIR/.checkpatch.conf"

cp "$CURDIR/pre-commit" "$HOOKDIR"
chmod +x  "$HOOKDIR/pre-commit"

cp "$CURDIR/commit-msg" "$HOOKDIR"
chmod +x "$HOOKDIR/commit-msg"

