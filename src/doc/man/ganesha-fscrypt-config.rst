.. SPDX-License-Identifier: LGPL-3.0-or-later

============================================================================
ganesha-fscrypt-config -- NFS Ganesha Export extension for fscrypt and kmip.
============================================================================

.. program:: ganesha-fscrypt-config


SYNOPSIS
==========================================================

    /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================

The plugin kmip_fscrypt allows the use of encrypted
filesystems from FSALs that support fscrypt operations.
To enable this plugin, in ganesha.conf add this line:

    %plugin kmip_fscrypt

Then add a KMIP block to specify the kmip host(s).
Then, for each export that is to be encrypted,
add a kmip_key_id value.

All parameters in the KMIP block as well as the per-export
kmip_key_id are dynamically updateable.

Since KMIP parameters are only used when an export is updated,
it is not necessary to SIGHUP ganesha immediately upon updating
these values, unless one is fixing a problem that caused exports
not to be available.

When Ganesha starts up or receives a SIGHUP, it attaches
each export, which is to say, it validates that it can reach
the resource, and makes it available to be exported.  This plugin
extends the attach operation to check to see if the root directory
is encrypted.  For encrypted directories, if no key is provided,
or the key does not match what was originally set, the export
is not made available.  If the directory is not encrypted, a key
should not be provided.  There is one special case here,
if the directory is empty but not encrypted, and a key is provided,
the directory is made encrypted by that key.  This normally only
happens the first time a directory that should be encrypted is attached.
Otherwise, if a key is provided for an unencrypted directory,
the export is not made available.

This file lists the extension config options.

KMIP {}
--------------------------------------------------------------------------------
This is the global block that contains parameters used to
communicate with the kmip server.

A client certificate (and key) are usually required to talk to kmip.
Some implementations may instead want a username and password.
Additionally, one or more HOST blocks must be provided.

If the client certificate was signed by an intermediate CA,
intermediate CA certificates may be stored in one of 3 places;
appended to the client certificate, stored in the ca file,
or kept in a separate cert_chain.

cert (optional)
    This is a file that should contain the client certificate in PEM format.
    Optionally, intermediate CA certificates may be added here.

key (required if cert is supplied)
    Client's private key in PEM format.

ca (required)
    This should contain one or more certificates in PEM format.
    This is used to vet the server's certificate, a hard requirement.
    May also contain the client's intermediate certificates.
    If the server certificate is self-signed, this file should
    contain a copy of the self-signed certificate.

cert_chain (optional)
    A file containing one more intermediate CA certificates in PEM format
    for use with the client certificate.

user (optional)
    If the kmip server requires a username and password, the username.

password (optional)
    If the kmip server requires a username and password, the password.

protocol (enum, default 1.1)
    Possible values 1.0, 1.1, 1.2, 1.3, 1.4, 2.0.

idle_timeout (uint32, range 1 to 3600, default 7)
    Number of seconds to hold an idle connection to kmip before closing it.
    Most kmip implementations seem to start logging complaints
    if a connection is not closed within 10 seconds.

HOST {}
--------------------------------------------------------------------------------
One or more HOST sub-blocks should be specified inside the KMIP block.
Ganesha will iterate through the list of hosts until it finds
one that it connect to.  This host will be favored for
subsequent uses.

Each block can contain at most one of each of the following keys:

addr (default localhost)
    The (fqdn) DNS name (or with other options IP address) of the kmip
    server.  Note: you'll likely need the verify_hostname or servername
    options if connecting with an IP address.

port(uint16, default 5696)
    The port on which the kmip server is listening.

servername (optional, defaults to addr)
    When Ganesha connects to the kmip server, it will advertise
    this name to the KMIP server (via SNI) to indicate which
    server certificate it wants.  If set to "" no name is advertised.

verify_hostname (optional, defaults to servername or addr)
    This is the name that Ganesha expects to see in the server
    certificate that comes back from the kmip server, either
    as a subject alternate name, or if none are present, the certificate's subject DN.
    If set to "" security is reduced; any server certificate signed by
    the CA will be accepted.

EXPORT { }
--------------------------------------------------------------------------------
This plugin adds one value that can be specified in the
**EXPORT** block for any export.  It will only be useful for
FSALs that support fscrypt.  As of this writing, this only works for CEPH.

kmip_key_id(optional)
    This is a string that should be the unique ID of the kmip key.
    Ganesha does not do a locate function, so beware: this is the fixed
    immutable ID assigned by the kmip server, and not a Name attribute.

SEE ALSO
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
:doc:`ganesha-ceph-config <ganesha-ceph-config>`\(8)
