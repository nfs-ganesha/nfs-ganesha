#!/usr/bin/env python

from __future__ import print_function
import json,sys

if len(sys.argv) > 1:
    GERRIT_PROJECT = sys.argv[1]
else:
    GERRIT_PROJECT = u'ffilz/nfs-ganesha'
debug = False

for line in sys.stdin:
    try:
        obj=json.loads(line)
    except ValueError:
        # silently skip what wasn't json
        continue

    if obj[u'type'] != 'patchset-created' or obj[u'change'][u'project'] != GERRIT_PROJECT:
        # skip events we don't care about
        continue

    if debug:
        print(json.dumps(obj, indent=4), file=sys.stderr)


    # format stuff so we can read it in shell easily
    print("%s %s" % (obj[u'patchSet'][u'ref'], obj[u'patchSet'][u'revision']))
    # need to flush if printing to a pipe
    sys.stdout.flush()
