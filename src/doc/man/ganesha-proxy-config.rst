===================================================================
ganesha-proxy-config -- NFS Ganesha Proxy Configuration File
===================================================================

.. program:: ganesha-proxy-config


SYNOPSIS
==========================================================

| /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================

NFS-Ganesha install the following config file for Proxy FSAL:
| /etc/ganesha/ganesha.conf

This file lists Proxy specific config options.

EXPORT { FSAL {} }
--------------------------------------------------------------------------------

Name(string, "proxy")
    Name of FSAL should always be proxy.

**Retry_SleepTime(uint32, range 0 to 60, default 10)**

**Srv_Addr(ipv4_addr default "127.0.0.1")**

**NFS_Service(uint32, range 0 to UINT32_MAX, default 100003)**

**NFS_SendSize**
	must be greater than maxwrite+SEND_RECV_HEADER_SPACE

**NFS_RecvSize**
	must be greater than maxread+SEND_RECV_HEADER_SPACE

**MAX_READ_WRITE_SIZE(default 1 MB)**

**SEND_RECV_HEADER_SPACE(default 512 Bytes)**

**FSAL_MAXIOSIZE(default 64 MB)**

NFS_SendSize(uint64, default MAX_READ_WRITE_SIZE + SEND_RECV_HEADER_SPACE)
    range 512 + SEND_RECV_HEADER_SPACE to FSAL_MAXIOSIZE

NFS_RecvSize(uint64, default MAX_READ_WRITE_SIZE + SEND_RECV_HEADER_SPACE)
    range 512 + SEND_RECV_HEADER_SPACE to FSAL_MAXIOSIZE

**NFS_Port(uint16, range 0 to UINT16_MAX, default 2049)**

**Use_Privileged_Client_Port(bool, default true)**

**RPC_Client_Timeout(uint32, range 1 to 60*4, default 60)**

**Remote_PrincipalName(string, no default)**

**KeytabPath(string, default "/etc/krb5.keytab")**

**Credential_LifeTime(uint32, range 0 to 86400*2, default 86400)**

**Sec_Type(enum, values [krb5, krb5i, krb5p], default krb5)**

**Active_krb5(bool, default false)**

**Enable_Handle_Mapping(bool, default false)**

**HandleMap_DB_Dir(string, default "/var/ganesha/handlemap")**

**HandleMap_Tmp_Dir(string, default "/var/ganesha/tmp")**

**HandleMap_DB_Count(uint32, range 1 to 16, default 8)**

**HandleMap_HashTable_Size(uint32, range 1 to 127, default 103)**

See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
