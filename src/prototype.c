Procedure to handle an unlock 
====================== 

Make a copy of the unlock in a special new lock list 
For each lock in global  lock list that matches file 
        if the lock belongs to the same owner, handle the unlock of that owner's locks 
        otherwise, use the other owner's lock to "remove" that lock from the special lock list 
                (this will work similar to how unlock works on the regular lock list) 
At the end, the special lock list is the set of unlocks that must be propagated to the kernel 

Procedure to handle a lock request 
========================= 

Push the lock into the kernel (this may or may not actually change anything), if it would conflict, return failure (what about blocking locks...) 
Handle lock within Ganesha as usual 

Procedure to handle a blocking lock 
========================== 
This is not resolved, since clients don't trust servers, we may be able to just hand wave here and rely on the client's backup polling logic. 

This design does not resolve integration with NFS v4, nor does it propagate NFS v4 locks into the filesystem/kernel. 

There is a larger question if some of this logic should actually be pushed into the FSAL, perhaps extending the FSAL locking interface to support the full range of NLM v4 and NFS v4 locks. Conceivably, an FSAL might support integration of larger set of locks than just byte range, across a cluster, or with other applications/servers (such as Samba for example). Such an integration layer might well be able to distinguish between multiple lock owners originating from the same process. 

Frank
