.. SPDX-License-Identifier: LGPL-3.0-or-later

=========================================================================
ganesha-tls-config -- NFS-Ganesha TLS Service Configurations file
=========================================================================

.. program:: ganesha-tls-config

SYNOPSIS
==========================================================

| /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================
Transport Layer Security (TLS) is a cryptographic protocol that provides
confidentiality, integrity, and authentication for network communications.
When used with NFS, TLS encrypts all traffic between the client and server,
ensuring that data is not visible to intermediaries and that it cannot be
altered in transit without detection.

TLS operates at the transport layer (above TCP) and establishes a secure
channel between the client and server before any application data is exchanged.
During the handshake process, the client validates the server's identity
using its certificate. The server may also optionally request a certificate
from the client.

When NFS is configured to use TLS-based transport security, the mount
negotiation and all subsequent RPC calls are carried over this encrypted
channel.

TLS VS. MTLS
In standard TLS (also called one-way TLS), only the server is authenticated.
The client verifies that it is talking to the correct server by validating
the server's certificate against a trusted Certificate Authority (CA).
The server does not verify the client's identity beyond the network address
and any application-level authentication.

In mutual TLS (mTLS), both endpoints present certificates during the TLS
handshake. The client still validates the server's certificate, but the
server also validates the client's certificate against its own trusted CA
list. This provides stronger assurance that the client is an authorized
entity, not just any host on the network. A client certificate signed by a
trusted CA is required. This mode is used where unauthorized clients must be
blocked before any data exchange occurs.

Below are the ways by which client can initiate NFS over TLS connection while mounting:

**Example 1: Stunnel (Install stunnel)**

1. Configure stunnel with forwarding port to ganesha configured port.

   Example config file (/etc/stunnel/stunnel.conf)::

     pid = /var/run/stunnel/stunnel.pid
     cert = /home/dpp/workspace/TLS_ceritificates/vm1.crt
     key = /home/dpp/workspace/TLS_ceritificates/vm1.key
     CAfile = /home/dpp/workspace/TLS_ceritificates/ca.crt
     socket = r:TCP_NODELAY=1
     foreground = yes
     debug = 7
     output = /var/log/stunnel.log
     renegotiation = no

     [nfs4]
     client = yes
     accept = 127.0.0.1:49152
     connect = 23.13.161.10:2049
     sslVersion = TLSv1.3
     ciphers = ALL
     verify = 2
     # change above verify = 2 for tls and mtls accordingly.

2. Start stunnel

3. Mount using below command::

     mount -vv -t nfs -o rw -o port=49152 127.0.0.1:/exportfs0 /mnt/

**Example 2: Use the tlshd (Install ktls-utils)**

1. Configure /etc/tlshd.conf
2. Start tlshd
3. Mount command::

     mount -vv -t nfs -o vers=4 -o xprtsec=mtls -o rw 104.86.87.87:/exportfs0/ /mnt

OPTIONS
------------------------------------------------------------------------------

Note : EXPORT{} needs to be populated with proper XprtSec = none or tls or mtls
or can be provided in EXPORT_DEFAULTS {XprtSec = none/tls/mtls}
This allows controlling of access at Export level.

=============================  ===================  =============================================================
Server Export Security         Client Mount Option  Expected Behavior
=============================  ===================  =============================================================
XprtSec=none                   (no xprtsec)         Mount succeeds in plaintext (unencrypted TCP/UDP).
XprtSec=none                   xprtsec=tls          Mount succeeds over TLS; encryption used even though not required.
XprtSec=none                   xprtsec=mtls         Mount succeeds over TLS; encryption used even though not required.
XprtSec=tls                    (no xprtsec)         Mount fails with EACCES ("Permission denied") - server rejects non-TLS connections.
XprtSec=tls                    xprtsec=tls          Mount succeeds over TLS; server certificate validated by client.
XprtSec=tls                    xprtsec=mtls         Mount succeeds over TLS; Even if client certificate fails, it doesnt matter.
XprtSec=mtls                   (no xprtsec)         Mount fails with EACCES - client not attempting mTLS.
XprtSec=mtls                   xprtsec=tls          Mount fails with EACCES - mTLS required, i.e client should provide valid certificate to server.
XprtSec=mtls                   xprtsec=mtls         Mount succeeds only if client presents valid certificate trusted by server; otherwise fails with EACCES.
=============================  ===================  =============================================================


TLS_CONFIG {}
-----------------------------------------------------------------------------
       Enable_TLS(bool, default false)

       TLS_CA_File(path, default "")
        eg : TLS_CA_File = "/etc/ganesha/tls/ca.crt";

       TLS_Cert_File(path, default "")
        eg: TLS_Cert_File = "/etc/ganesha/tls/ganesha.crt";

       TLS_Key_File(path, default "")
        eg : TLS_Key_File = "/etc/ganesha/tls/ganesha.key";

       TLS_Ciphers(string, default NULL)
        eg for backend as openssl :
               TLS_Ciphers = "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
        eg for backend as gnutls :
               TLS_Ciphers = ":-CIPHER-ALL:+AES-256-GCM";

       TLS_Min_Version(string, "TLSv1.3")

       options = "TLSv1.3", "TLSv1.2";
        eg : TLS_Min_Version = "TLSv1.3"

       Enable_KTLS(bool, default false)

       To enable and disable KTLS transferes.
        Note : Currently applicable for only openssl.
        If user want to enable/disable ktls for gnutls use gnutls config file.

       Enable_debug(bool, default false)
        To enable TLS library callback prints and full debugging logs for TLS.

NOTES:
-------------------------------------------------------------------------------
This module provides user with flexibility of using openssl or gnutls as backend
but as of now its a compilation option.
USE_TLS to compile in NFS-ganesha with TLS.

Dependency:

        USE_OPENSSL to use openssl as TLS lib.
        USE_GNUTLS to use GnuTLS as TLS lib.

AUTHOR
-------------------------------------------------------------------------------
NFS-Ganesha-TLS feature added by Deeraj.Patil@ibm.com

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
