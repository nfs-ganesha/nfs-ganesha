#!/usr/bin/awk -f

# run with -v debug=1 for verbose printing

/^change / {
    for (i in patchSets) {
        if (debug) for (j in patchSets[i]) {
            print i, j, patchSets[i][j]
        }
        if (!patchSets[i]["reviewed"]) {
            print patchSets[i]["ref"] " " patchSets[i]["commit"]
        }
    }
    delete patchSets;
    curSet=0;
}
/^ *username:/ { username=$2 }
/^ *message: Patch Set/ {
    if (username == "ganesha-triggers") {
        sub(":", "", $4);
        patchSets[$4]["reviewed"] = "ok"
    }
}
/^ *number: / { curSet=$2 }
/^ *ref: / { patchSets[curSet]["ref"]=$2 }
/^ *revision: / { patchSets[curSet]["commit"]=$2 }
