#!/usr/bin/env python2

import nfs4lib

ncl = nfs4lib.create_client("citi-1", 2049, "udp")
operations = []
operations.append(ncl.putrootfh_op())
operations.extend(ncl.lookup_path(["nfs4st", "doc", "README"]))
res = ncl.compound(operations)
