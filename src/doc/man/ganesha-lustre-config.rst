===================================================================
ganesha-lustre-config -- NFS Ganesha LUSTRE Configuration File
===================================================================

.. program:: ganesha-lustre-config


SYNOPSIS
==========================================================

| /etc/ganesha/lustre.conf

DESCRIPTION
==========================================================

NFS-Ganesha installs the config example for LUSTRE FSAL:
| /etc/ganesha/lustre.conf

This file lists LUSTRE specific config options.

EXPORT { FSAL {} }
--------------------------------------------------------------------------------

Name(string, "lustre")
    Name of FSAL should always be lustre.

**async_hsm_restore(bool, default true)**

All options of VFS export and module could be used for a FSAL_LUSTRE export and module.
:doc:`ganesha-vfs-config <ganesha-vfs-config>`\(8)

See also
==============================
:doc:`ganesha-vfs-config <ganesha-vfs-config>`\(8)
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
