#!/bin/sh

if [[ $OSTYPE == 'darwin'* ]]; then
	CURDIR=$(dirname "$(greadlink -m "$0")")
	TOPDIR=$(git rev-parse --show-toplevel)
	HOOKDIR=$TOPDIR/.git/hooks
else
	CURDIR=$(dirname "$(readlink -m "$0")")
	TOPDIR=$(git rev-parse --show-toplevel)
	HOOKDIR=$TOPDIR/.git/hooks
fi

# Link checkpatch script configuration file to top level working
# directory.
ln -sf ./src/scripts/checkpatch.conf "$TOPDIR/.checkpatch.conf"

cp -f "$CURDIR/pre-commit" "$HOOKDIR"
chmod +x  "$HOOKDIR/pre-commit"

cp -f "$CURDIR/commit-msg" "$HOOKDIR"
chmod +x "$HOOKDIR/commit-msg"

