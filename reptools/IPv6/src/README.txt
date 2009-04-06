nfs@scratchy.ocre.cea.fr (GSS_C_NT_HOSTBASED_SERVICE) <==> nfs/scratchy.ocre.cea.fr@SIMPSONS.OCRE.CEA.FR

GSSRPC:
---------------

On scratchy (Fedora 9, i686, lastest updates):
 ./toto-server-gssrpc -S nfs@scratchy.ocre.cea.fr

On itchy (CentOS5, i686, latest updates:
(do not forget to make a kinit before)
  ./toto-client-gssrpc -S nfs@scratchy.ocre.cea.fr -d scratchy

On scratchy (a NFSv4 configured server, using krb5 and rpc.svcgssd is running
on itchy) with credentials acquired for nfs/scratchy.ocre.cea.fr
 ./toto-client-gssrpc-nfs4 -S nfs@itchy.ocre.cea.fr -d itchy

RPCSEC_GSS:
-------------

Same command lines than with gssrpc:
scratchy#  ./toto-server-rpcsecgss -S nfs@scratchy.ocre.cea.fr
itchy#  ./toto-client-rpcsecgss-S nfs@scratchy.ocre.cea.fr -d scratchy

Nothings happens... Apparently no communication beteen client and server

Cross tests:
The GSSRPC NFSv4 clients works in front of the nfs server (using rpc.svcgssd
with librpcsecgss), so implementations are compatible.

Test1:
-----
 ./toto-server-gssrpc  + ./toto-client-rpcsecgss 
clnttcp_call failed on client. Error is 'Can't encode arguments'
I set the debug level to a high value (10), I have this in /var/log/messages:

Aug  8 09:31:28 scratchy toto-client-rpcsecgss: rpcsec_gss: gss_get_mic:
(major) Unspecified GSS failure.  Minor code may provide more information -
(minor) Cannot allocate memory
Aug  8 09:31:41 scratchy toto-client-rpcsecgss: rpcsec_gss: gss_get_mic:
(major) Unspecified GSS failure.  Minor code may provide more information -
(minor) Cannot allocate memory
  


Test2:
------
  ./toto-server-rpcsecgss +  ./toto-client-gssrpc

  Server crashes with segfault:
     
Program received signal SIGSEGV, Segmentation fault.
0x00af2fd6 in memcpy () from /lib/libc.so.6
Missing separate debuginfos, use: debuginfo-install e2fsprogs.i386 glibc.i686
keyutils.i386 krb5.i386 libgssglue.i386 libselinux.i386
(gdb) where
#0  0x00af2fd6 in memcpy () from /lib/libc.so.6
#1  0x0956d210 in ?? ()
#2  0x00a0d9d9 in kg_seal () from /usr/lib/libgssapi_krb5.so.2
#3  0x00a1125f in krb5_gss_seal () from /usr/lib/libgssapi_krb5.so.2
#4  0x00a10097 in ?? () from /usr/lib/libgssapi_krb5.so.2
#5  0x00a0073a in gss_seal () from /usr/lib/libgssapi_krb5.so.2
#6  0x00a00797 in gss_wrap () from /usr/lib/libgssapi_krb5.so.2
#7  0x00125107 in gss_wrap () from /usr/lib/libgssglue.so.1
#8  0x0011561c in xdr_rpc_gss_wrap_data (xdrs=0x9565cb0, xdr_func=0x8048be0
<xdr_int@plt>, xdr_ptr=0xbf875104 "\003", ctx=0x9567d90, qop=0, 
    svc=RPCSEC_GSS_SVC_PRIVACY, seq=1) at authgss_prot.c:194
#9  0x00115a12 in xdr_rpc_gss_data (xdrs=0x9565cb0, xdr_func=0x8048be0
<xdr_int@plt>, xdr_ptr=0xbf875104 "\003", ctx=0x9567d90, qop=0, 
    svc=RPCSEC_GSS_SVC_PRIVACY, seq=1) at authgss_prot.c:293
#10 0x00118137 in svcauth_gss_wrap (auth=0x9567d80, xdrs=0x9565cb0,
xdr_func=0x8048be0 <xdr_int@plt>, xdr_ptr=0xbf875104 "\003")
    at svc_auth_gss.c:701
#11 0x001193fd in svctcp_reply (xprt=0x9567250, msg=0xbf8750a8) at
svc_tcp.c:520
#12 0x00116418 in rpcsecgss_svc_sendreply (xprt=0xbf874fcc,
xdr_results=0x8048be0 <xdr_int@plt>, xdr_location=0xbf875104 "\003") at
svc.c:269
#13 0x08048fbb in dispatch (ptr_req=0xbf875858, ptr_svc=0x9567250) at
toto-server-rpcsecgss.c:81
#14 0x00116808 in rpcsecgss_svc_getreqset2 (readfds=0x95678e0, width=12) at
svc.c:482
#15 0x00116a70 in rpcsecgss_svc_run () at svc_run.c:80
#16 0x0804942a in main (argc=3, argv=0xbf876f04) at
toto-server-rpcsecgss.c:220





