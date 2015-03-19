#!/bin/bash -u

# configuration
GERRIT_SERVER=review.gerrithub.io
GERRIT_USER=ganesha-triggers
GERRIT_KEYFILE=$HOME/.ssh/id_rsa_ganesha-triggers
PROJECT=ffilz/nfs-ganesha

# host box configuration needed:
# - checkpatch.conf configured (.checkpatch.conf in homedir)
# - current repository has a remote called 'gerrit' setup
# - ssh configuration that said 'gerrit' remote doesn't need a password
# - awk script for oneshot query requires awk >= 4

# internal variables
SSH_GERRIT="ssh -p 29418 -i $GERRIT_KEYFILE -l $GERRIT_USER $GERRIT_SERVER"
DRYRUN=""
ONESHOT=""

while getopts :nqch opt; do
    case "$opt" in
        n)
            DRYRUN=1
            ;;
        q)
            ONESHOT=query
            ;;
        c)
            ONESHOT=cat
            ;;
        *)
            cat << EOF
Usage: $0 [-n] [-o]

    -n  dry-run mode, do not actually push reviews
    -q  one-shot queries gerrit and tries to catch up on open
        patchset that were not commented
    -c  one-shot cat, reads one line at a time "ref commit"
        e.g "refs/change/x/y/z 0123456789abcdef"
EOF
            exit
            ;;
    esac
done


input_loop() {
    case "$ONESHOT" in
    "query")
        $SSH_GERRIT "gerrit query --comments --patch-sets \
                                  is:open project:$PROJECT" | \
        awk -f gerrit-query.awk
        ;;
    "cat")
        cat
        ;;
    *)
        while date 1>&2; do
            $SSH_GERRIT "gerrit stream-events" | \
            python gerrit-stream-filter.py $PROJECT
        done
        ;;
    esac
}

commit_review() {
    if [[ -z "$DRYRUN" ]]; then
        tee >($SSH_GERRIT "gerrit review --json \
                                         --project $PROJECT $COMMIT") 1>&2
    else
        echo "Would have submit:"
        cat
    fi
}

input_loop | \
while read REF COMMIT; do
    echo "got ref $REF, commit $COMMIT"
    git fetch gerrit $REF >/dev/null 2>&1
    git show --format=email $COMMIT         | \
        ../checkpatch.pl -q -               | \
        python checkpatch-to-gerrit-json.py | \
        commit_review
done
