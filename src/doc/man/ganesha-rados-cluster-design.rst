==============================================================================
ganesha-rados-cluster-design -- Clustered RADOS Recovery Backend Design
==============================================================================

Overview
--------
This document aims to explain the theory and design behind the
rados_cluster recovery backend, which coordinates grace period
enforcement among multiple, independent NFS servers.

In order to understand the clustered recovery backend, it's first necessary
to understand how recovery works with a single server:

Singleton Server Recovery
-------------------------
NFSv4 is a lease-based protocol. Clients set up a relationship to the
server and must periodically renew their lease in order to maintain
their ephemeral state (open files, locks, delegations or layouts).

When a singleton NFS server is restarted, any ephemeral state is lost. When
the server comes comes back online, NFS clients detect that the server has
been restarted and will reclaim the ephemeral state that they held at the
time of their last contact with the server.

Singleton Grace Period
----------------------
In order to ensure that we don't end up with conflicts, clients are
barred from acquiring any new state while in the Recovery phase. Only
reclaim operations are allowed.

This period of time is called the **grace period**. Most NFS servers
have a grace period that lasts around two lease periods, however
nfs-ganesha can and will lift the grace period early if it determines
that no more clients will be allowed to recover.

Once the grace period ends, the server will move into its Normal
operation state. During this period, no more recovery is allowed and new
state can be acquired by NFS clients.

Reboot Epochs
-------------
The lifecycle of a singleton NFS server can be considered to be a series
of transitions from the Recovery period to Normal operation and back. In the
remainder of this document we'll consider such a period to be an
**epoch**, and assign each a number beginning with 1.

Visually, we can represent it like this, such that each
Normal -> Recovery transition is marked by a change in the epoch value:

::

    +-------+-------+-------+---------------+-------+
    | State | R | N | R | N | R | R | R | N | R | N |
    +-------+-------+-------+---------------+-------+
    | Epoch |   1   |   2   |       3       |   4   |
    +-------+-------+-------+---------------+-------+

Note that it is possible to restart during the grace period (as shown
above during epoch 3). That just serves to extend the recovery period
and the epoch. A new epoch is only declared during a Recovery -> Normal
transition.

Client Recovery Database
------------------------
There are some potential edge cases that can occur involving network
partitions and multiple reboots. In order to prevent those, the server
must maintain a list of clients that hold state on the server at any
given time. This list must be maintained on stable storage. If a client
sends a request to reclaim some state, then the server must check to
make sure it's on that list before allowing the request.

Thus when the server allows reclaim requests it must always gate it
against the recovery database from the previous epoch. As clients come
in to reclaim, we establish records for them in a new database
associated with the current epoch.

The transition from recovery to normal operation should perform an
atomic switch of recovery databases. A recovery database only becomes
legitimate on a recovery to normal transition. Until that point, the
recovery database from the previous epoch is the canonical one.

Exporting a Clustered Filesystem
--------------------------------
Let's consider a set of independent NFS servers, all serving out the same
content from a clustered backend filesystem of any flavor. Each NFS
server in this case can itself be considered a clustered FS client. This
means that the NFS server is really just a proxy for state on the
clustered filesystem.

The filesystem must make some guarantees to the NFS server. First filesystem
guarantee:

1. The filesystem ensures that the NFS servers (aka the FS clients)
   cannot obtain state that conflicts with that of another NFS server.

This is somewhat obvious and is what we expect from any clustered filesystem
outside of any requirements of NFS. If the clustered filesystem can
provide this, then we know that conflicting state during normal
operations cannot be granted.

The recovery period has a different set of rules. If an NFS server
crashes and is restarted, then we have a window of time when that NFS
server does not know what state was held by its clients.

If the state held by the crashed NFS server is immediately released
after the crash, another NFS server could hand out conflicting state
before the original NFS client has a chance to recover it.

This *must* be prevented. Second filesystem guarantee:

2. The filesystem must not release state held by a server during the
   previous epoch until all servers in the cluster are enforcing the
   grace period.

In practical terms, we want the filesystem to provide a way for an NFS
server to tell it when it's safe to release state held by a previous
instance of itself. The server should do this once it knows that all of
its siblings are enforcing the grace period.

Note that we do not require that all servers restart and allow reclaim
at that point. It's sufficient for them to simply begin grace period
enforcement as soon as possible once one server needs it.

Clustered Grace Period Database
-------------------------------
At this point the cluster siblings are no longer completely independent,
and the grace period has become a cluster-wide property. This means that
we must track the current epoch on some sort of shared storage that the
servers can all access.

Additionally we must also keep track of whether a cluster-wide grace period
is in effect. Any running nodes should all be informed when either of this
info changes, so they can take appropriate steps when it occurs.

In the rados_cluster backend, we track these using two epoch values:

**C**: is the current epoch. This represents the current epoch value
         of the cluster

**R**: is the recovery epoch. This represents the epoch from which
         clients are allowed to recover. A non-zero value here means
         that a cluster-wide grace period is in effect. Setting this to
         0 ends that grace period.

In order to decide when to make grace period transitions, each server must
also advertise its state to the other nodes. Specifically, each server must
be able to determine these two things about each of its siblings:

1. Does this server have clients from the previous epoch that will require
   recovery? (NEED)

2. Is this server enforcing the grace period by refusing non-reclaim locks?
   (ENFORCING)

We do this with a pair of flags per sibling (NEED and ENFORCING). Each
server typically manages its own flags.

The rados_cluster backend stores all of this information in a single
RADOS object that is modified using read/modify/write cycles. Typically
we'll read the whole object, modify it, and then attept to write it
back. If something changes between the read and write, we redo the read
and try it again.

Clustered Client Recovery Databases
-----------------------------------
In rados_cluster the client recovery databases are stored as RADOS
objects. Each NFS server has its own set of them and they are given
names that have the current epoch (C) embedded in it. This ensures
that recovery databases are specific to a particular epoch.

In general, it's safe to delete any recovery database that precedes R
when R is non-zero, and safe to remove any recovery database except for
the current one (the one with C in the name) when the grace period is
not in effect (R==0).

Establishing a New Grace Period
-------------------------------
When a server restarts and wants to allow clients to reclaim their
state, it must establish a new epoch by incrementing the current epoch
to declare a new grace period (R=C; C=C+1).

The exception to this rule is when the cluster is already in a grace
period. Servers can just join an in-progress grace period instead of
establishing a new one if one is already active.

In either case, the server should also set its NEED and ENFORCING flags
at the same time.

The other surviving cluster siblings should take steps to begin grace
period enforcement as soon as possible. This entails "draining off" any
in-progress state morphing operations and then blocking the acquisition
of any new state (usually with a return of NFS4ERR_GRACE to clients that
attempt it). Again, there is no need for the survivors from the previous
epoch to allow recovery here.

The surviving servers must however establish a new client recovery
database at this point to ensure that their clients can do recovery in
the event of a crash afterward.

Once all of the siblings are enforcing the grace period, the recovering
server can then request that the filesystem release the old state, and
allow clients to begin reclaiming their state. In the rados_cluster
backend driver, we do this by stalling server startup until all hosts
in the cluster are enforcing the grace period.

Lifting the Grace Period
------------------------
Transitioning from recovery to normal operation really consists of two
different steps:

1. the server decides that it no longer requires a grace period, either
   due to it timing out or there not being any clients that would be
   allowed to reclaim.

2. the server stops enforcing the grace period and transitions to normal
   operation

These concepts are often conflated in singleton servers, but in a cluster
we must consider them independently.

When a server is finished with its own local recovery period, it should
clear its NEED flag. That server should continue enforcing the grace
period however until the grace period is fully lifted. The server must
not permit reclaims after clearing its NEED flag, however.

If the servers' own NEED flag is the last one set, then it can lift the
grace period (by setting R=0). At that point, all servers in the cluster
can end grace period enforcement, and communicate that fact to the
others by clearing their ENFORCING flags.
