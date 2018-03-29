===================================================================
ganesha-config -- NFS Ganesha Configuration File
===================================================================

.. program:: ganesha-config


SYNOPSIS
==========================================================

| /etc/ganesha/ganesha/conf

DESCRIPTION
==========================================================

NFS-Ganesha obtains configuration data from the configuration file:

    /etc/ganesha/ganesha.conf

The configuration file constitues of following parts:

Comments
--------------------------------------------------------------------------------
Empty lines and lines starting with ‘#’ are comments.::

    # This whole line is a comment
    Protocol = TCP; # The rest of this line is a comment

Blocks
--------------------------------------------------------------------------------
Related options are grouped together into "blocks".
A block is a name followed by parameters enclosed between "{"
and "}".
A block can contain other sub blocks as well.::

    Export
    {
        Export_ID = 1;
        FSAL {
            Name = VFS:
        }
    }

NOTE: FSAL is a sub block.
Refer to ``BLOCKS`` section for list of blocks and options.

Options
--------------------------------------------------------------------------------
Configuration options can be of following types.

1. **Numeric** Numeric options can be defined in octal, decimal, or hexadecimal.
The format follows ANSI C syntax.
eg.::

    mode = 07555;  # This is octal 0755, 493 (decimal)

Numeric values can also be negated or logical NOT'd.
eg.::

    anonomousuid = -2; # this is a negative
    mask = ~0xff; # Equivalent to 0xffffff00 (for 32 bit integers)

2. **Boolean** Possible values are true, false, yes and no.
1 and 0 are not acceptable.

3. **List** The option can contain a list of possible applicable values.
Protocols = 3, 4, 9p;


Including other config files
--------------------------------------------------------------------------------
Additional files can be referenced in a configuration using %include
and %url directives.::

	%include <filename>
	%url <url, e.g., rados://mypool/myobject>

The included file is inserted into the configuration text in place of
the %include or %url line. The configuration following the inclusion
is resumed after the end of the included files. File inclusion can be
to any depth.

eg.::
    %include base.conf
    %include "base.conf"
    %url rados://mypool/myobject
    %url "rados://mypool/myobject"


BLOCKS
==========================================================
NFS-Ganesha supports the following blocks:

EXPORT {}
--------------------------------------------------------------------------------
Along with configuration options, it also support two subblocks:
1.**EXPORT { FSAL {} }**
2.**EXPORT { CLIENT  {} }**

Refer to :doc:`ganesha-export-config <ganesha-export-config>`\(8) for usage
that this block and its sub blocks support.

EXPORT_DEFAULTS {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-export-config <ganesha-export-config>`\(8) for usage

CACHEINODE {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-cache-config <ganesha-cache-config>`\(8) for usage

NFS_CORE_PARAM {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-core-config <ganesha-core-config>`\(8) for usage

NFS_IP_NAME {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-core-config <ganesha-core-config>`\(8) for usage

NFS_KRB5 {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-core-config <ganesha-core-config>`\(8) for usage

NFSv4 {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-core-config <ganesha-core-config>`\(8) for usage

CEPH {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-ceph-config <ganesha-ceph-config>`\(8) for usage

9P {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-9p-config <ganesha-9p-config>`\(8) for usage

GPFS {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-gpfs-config <ganesha-gpfs-config>`\(8) for usage

LOG {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-log-config <ganesha-log-config>`\(8) for usage

1.**LOG { FACILITY {} }**
2.**LOG { FORMAT {} }**

PROXY {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-proxy-config <ganesha-proxy-config>`\(8) for usage

1.**PROXY { Remote_Server {} }**

RGW {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-rgw-config <ganesha-rgw-config>`\(8) for usage

VFS {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-vfs-config <ganesha-vfs-config>`\(8) for usage

XFS {}
--------------------------------------------------------------------------------
Refer to :doc:`ganesha-xfs-config <ganesha-xfs-config>`\(8) for usage


EXAMPLE
==========================================================
Along with "ganesha.conf", for each installed FSAL, a sample config file is added at:

| /etc/ganesha


See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-rgw-config <ganesha-rgw-config>`\(8)
:doc:`ganesha-vfs-config <ganesha-vfs-config>`\(8)
:doc:`ganesha-xfs-config <ganesha-xfs-config>`\(8)
:doc:`ganesha-gpfs-config <ganesha-gpfs-config>`\(8)
:doc:`ganesha-gluster-config <ganesha-gluster-config>`\(8)
:doc:`ganesha-9p-config <ganesha-9p-config>`\(8)
:doc:`ganesha-proxy-config <ganesha-proxy-config>`\(8)
:doc:`ganesha-ceph-config <ganesha-ceph-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
