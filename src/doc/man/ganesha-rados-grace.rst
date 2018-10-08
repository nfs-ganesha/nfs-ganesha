======================================================================
ganesha-rados-grace -- manipulate the shared grace management database
======================================================================

SYNOPSIS
===================================================================

| ganesha-rados-grace [--ns namespace] [ --oid obj_id ] [ --pool pool_id ]  dump|add|start|join|lift|remove|enforce|noenforce|member [ nodeid ... ]

DESCRIPTION
===================================================================

This tool allows the administrator to directly manipulate the database
used by the rados_cluster recovery backend. Cluster nodes use that database to
indicate their current state in order to coordinate a cluster-wide grace
period.

The first argument should be a command to execute against the database.
Any remaining arguments represent the nodeids of nodes in the cluster
that should be acted upon.

Most commands will just fail if the grace database is not present. The
exception to this rule is the **add** command which will create the
pool, database and namespace if they do not already exist.

OPTIONS
===================================================================
**--ns**

Set the RADOS namespace to use within the pool (default is NULL)

**--oid**

Set the object id of the grace database RADOS object (default is "grace")

**--pool**

Set the RADOS poolid in which the grace database object resides (default is
"nfs-ganesha")

COMMANDS
===================================================================

**dump**

Dump the current status of the grace period database to stdout. This
will show the current and recovery epoch serial numbers, as well as a
list of hosts currently in the cluster and what flags they have set
in their individual records.

**add**

Add the specified hosts to the cluster. This must be done before the
given hosts can take part in the cluster. Attempts to modify the database
by cluster hosts that have not yet been added will generally fail. New
hosts are added with the enforcing flag set, as they are unable to hand
out new state until their own grace period has been lifted.

**start**

Start a new grace period. This will begin a new grace period in the
cluster if one is not already active and set the record for the listed
cluster hosts as both needing a grace period and enforcing the grace
period. If a grace period is already active, then this is equivalent
to **join**.

**join**

Attempt to join an existing grace period. This works like **start**, but
only if there is already an existing grace period in force.

**lift**

Attempt to lift the current grace period. This will clear the need grace
flags for the listed hosts. If there are no more hosts in the cluster
that require a grace period, then it will be fully lifted and the cluster
will transition to normal operations.

**remove**

Remove one or more existing hosts from the cluster. This will remove the
listed hosts from the grace database, possibly lifting the current grace
period if there are no more hosts that need one.

**enforce**

Set the flag for the given hosts that indicates that they are currently
enforcing the grace period; not allowing the acquisition of new state by
clients.

**noenforce**

Clear the enforcing flag for the given hosts, meaning that those hosts
are now allowing clients to acquire new state.

**member**

Test whether the given hosts are members of the cluster. Returns an
error if any of the hosts are not present in the grace db omap.

NODEID ASSIGNMENT
=================
Each running ganesha daemon requires a **nodeid** string that is unique
within the cluster. This can be any value as ganesha treats it as an opaque
string. By default, the ganesha daemon will use the hostname of the node where
it is running.

This may not be suitable when running under certain HA clustering
infrastructure, so it's generally recommended to manually assign nodeid values
to the hosts in the **RADOS_KV** config block of **ganesha.conf**.

GANESHA CONFIGURATION
=====================
The ganesha daemon will need to be configured with the RecoveryBackend
set to **rados_cluster**. If you use a non-default pool, namespace or
oid, nodeid then those values will need to be set accordingly in the
**RADOS_KV** config block as well.

STARTING A NEW CLUSTER
======================
First, add the given cluster nodes to the grace database. Assuming that the
nodes in our cluster will have nodeids ganesha-1 through ganesha-3:

**ganesha-rados-grace add ganesha-1 ganesha-2 ganesha-3**

Once this is done, you can start the daemons on each host and they will
coordinate to start and lift the grace periods as-needed.

ADDING NODES TO A RUNNING CLUSTER
=================================
After this point, new nodes can then be added to the cluster as needed using
the **add** command:

**ganesha-rados-grace add ganesha-4**

After the node has been added, ganesha.nfsd can then be started. It will
then request a new grace period as-needed.

REMOVING A NODE FROM THE CLUSTER
================================
To remove a node from the cluster, first unmount any clients that have
that node mounted (possibly moving them to other servers). Then execute the
remove command with the nodeids to be removed from the cluster. For example:

**ganesha-rados-grace remove ganesha-4**

This will remove the ganesha-4's record from the database, and possibly lift
the current grace period if one is active and it was the last one to need it.
