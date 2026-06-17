.. SPDX-License-Identifier: LGPL-3.0-or-later

=========================================================================
ganesha-qos-config -- NFS-Ganesha Quality of Service Configurations file
=========================================================================

.. program:: ganesha-qos-config

SYNOPSIS
==========================================================

| /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================
The nfs_qos module is part of the NFS-Ganesha server that ensures fair
distribution of I/O resources under load. It achieves this by using
rate limiting techniques such as bandwidth capping,
IOPS (Input/Output Operations Per Second) management, Token consumption.

The nfs_qos module is implemented in the source file nfs_qos.c.
It is designed to manage the quality of service for NFS operations by
controlling how many operations are executed over a specified interval.
The module balances the need for performance with the need to protect the
server from being overloaded by excessive or bursty I/O operations.

The module works in the following way:
    - Bandwidth and IOPS Limiting: The module monitors and regulates the amount
      of data transferred (bandwidth) and the number of ops executed (IOPS).
      And based on the configured limit's, calculates time required for the IO,
      and deffer the IO.
    - Token-Based Control: Each I/O operation requires tokens to be processed.
      Tokens are replenished periodically. If an incoming request does not have
      enough tokens available, IO will be retired slowely to client with
      NFS_ERROR_DELAY.
    - Can indvidually enable BandWidth, IOPS and Token controlling.

When making a decision on which QoS type to enable:
    - If simplicity and lower system overhead are the factors and the workload
      is fairly uniform, Per_Export might suffice. However, be aware that
      this can lead to client starvation.
    - If ensuring that a client’s queued I/Os don’t indefinitely delay new
      requests from that same client is a priority, Per_Client may help,
      although it does not globally balance load across different exports.
    - If fairness at both the export and client level is paramount and you can
      accommodate the additional complexity and resource usage, then the
      Per_Export_Per_Client approach is the most balanced strategy


OPTIONS
-------------------------------------------------------------------------------
The behavior of nfs_qos can be influenced by various configuration parameters,
typically set in the server’s main configuration file
(for example, /etc/ganesha/ganesha.conf). Some configurable options include:

- Maximum bandwidth: Set upper bounds on data transfer rates.
- Maximun IOPS limits: Set upper bounds for the number of operations executed
  per seconds.
- Token thresholds and refresh intervals: Define how many tokens available
  per time unit (usally 30 minutes, hourly, daily), which controls the rate of
  allowed data consuption over the specified time.

QOS_DEFAULT_CONFIG {}
-------------------------------------------------------------------------------

    enable_qos(bool, default false)

    enable_tokens(bool, default false)

    enable_bw_control(bool, default false)

    enable_iops_control(bool, default false)

    enable_ds_control(bool, default false)

    combined_rw_bw_control(bool, default false)

    combined_rw_token_control(bool, default true)

    combined_rw_iops_control(bool, default true)

    qos_type(enum, values ["Per_Export", "Per_Client", "Per_Export_Per_Client"], default "Per_Export_Per_Client")

    max_export_combined_bw(uint64, range 32768 to 107374182400, default 2147483648)

    max_client_combined_bw(uint64, range 32768 to 107374182400, default 2147483648)

    max_export_write_bw(uint64, range 32768 to 107374182400, default 2147483648)

    max_export_read_bw(uint64, range 32768 to 107374182400, default 2147483648)

    max_client_write_bw(uint64, range 32768 to 107374182400, default 2147483648)

    max_client_read_bw(uint64, range 32768 to 107374182400, default 2147483648)

    max_export_iops(uint64, range 8 to 1638400, default 8192)

    max_client_iops(uint64, range 8 to 1638400, default 8192)

    max_export_write_iops(uint64, range 8 to 1638400, default 8192)

    max_export_read_iops(uint64, range 8 to 1638400, default 8192)

    max_client_write_iops(uint64, range 8 to 1638400, default 8192)

    max_client_read_iops(uint64, range 8 to 1638400, default 8192)

    max_export_write_tokens(uint64, range 3774873600 to UINT64_MAX, default 90596966400)

    max_export_read_tokens(uint64, range 3774873600 to UINT64_MAX, default 90596966400)

    max_client_write_tokens(uint64, range 3774873600 to UINT64_MAX, default 90596966400)

    max_client_read_tokens(uint64, range 3774873600 to UINT64_MAX, default 90596966400)

    export_write_tokens_renew_time(uint64, range 3600 to UINT64_MAX, default 86400)

    export_read_tokens_renew_time(uint64, range 3600 to UINT64_MAX, default 86400)

    client_write_tokens_renew_time(uint64, range 3600 to UINT64_MAX, default 86400)

    client_read_tokens_renew_time(uint64, range 3600 to UINT64_MAX, default 86400)

EXPORT { QOS_BLOCK {} }
-------------------------------------------------------------------------------

    See global QOS_DEFAULT_CONFIG {}

    * Config exception is qos_type is not available at export level.

    * By default global EXPORT_DEFAULTS will be applied to export.

    * This block is required at export level only if there needs to be
      different value than global value.

    * Respective QOS_DEFAULT_CONFIG enable values needs to be enabled first
      to enable export level values.

    * If export QOS_BLOCK {} needs to be populated, then populate required
      enabled entities otherwise default value will get applied.

    * How range and default values derived can be found in src/include/nfs_qos.h

PSEUDOFS { QOS_BLOCK {} }
-------------------------------------------------------------------------------
   See EXPORT { QOS_BLOCK {} }

NOTES:
-------------------------------------------------------------------------------
The nfs_qos module is essential for maintaining a balanced and fair I/O workload
on the NFS server. By regulating operations with tokens, bandwidth,
and IOPS constraints, it protects the server from overload while ensuring
that client requests are handled in a controlled manner.
This man page provides an overview of its function and operation within the
greater context of the NFS server’s architecture.

AUTHOR
-------------------------------------------------------------------------------
NFS-Ganesha-QOS feature added by Deeraj.Patil@ibm.com, Naresh.Chillarege@ibm.com

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
