/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2012 IETF Trust and the persons identified
 * as authors of the code. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the
 * following conditions are met:
 *
 * o Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer in the documentation and/or other
 *   materials provided with the distribution.
 *
 * o Neither the name of Internet Society, IETF or IETF
 *   Trust, nor the names of specific contributors, may be
 *   used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS
 *   AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 *   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 *   EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *   NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *   IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 *   ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This code was derived from RFCTBD10.
 * Please reproduce this note if possible.
 */

/*
 * flex_files_prot.x
 */

/*
 * The following include statements are for example only.
 * The actual XDR definition files are generated separately
 * and independently and are likely to have a different name.
 * %#include <nfsv42.x>
 * %#include <rpc_prot.x>
 */

struct ff_device_versions4 {
        uint32_t        ffdv_version;
        uint32_t        ffdv_minorversion;
        uint32_t        ffdv_rsize;
        uint32_t        ffdv_wsize;
        bool            ffdv_tightly_coupled;
};

struct ff_device_addr4 {
        multipath_list4     ffda_netaddrs;
        ff_device_versions4 ffda_versions<>;
};

const FF_FLAGS_NO_LAYOUTCOMMIT   = 0x00000001;
const FF_FLAGS_NO_IO_THRU_MDS    = 0x00000002;
const FF_FLAGS_NO_READ_IO        = 0x00000004;
const FF_FLAGS_WRITE_ONE_MIRROR  = 0x00000008;
typedef uint32_t            ff_flags4;

struct ff_data_server4 {
    deviceid4               ffds_deviceid;
    uint32_t                ffds_efficiency;
    stateid4                ffds_stateid;
    nfs_fh4                 ffds_fh_vers<>;
    fattr4_owner            ffds_user;
    fattr4_owner_group      ffds_group;
};

struct ff_mirror4 {
    ff_data_server4         ffm_data_servers<>;
};

struct ff_layout4 {
    length4                 ffl_stripe_unit;
    ff_mirror4              ffl_mirrors<>;
    ff_flags4               ffl_flags;
    uint32_t                ffl_stats_collect_hint;
};

struct ff_ioerr4 {
        offset4        ffie_offset;
        length4        ffie_length;
        stateid4       ffie_stateid;
        device_error4  ffie_errors<>;
};

struct ff_io_latency4 {
        uint64_t       ffil_ops_requested;
        uint64_t       ffil_bytes_requested;
        uint64_t       ffil_ops_completed;
        uint64_t       ffil_bytes_completed;
        uint64_t       ffil_bytes_not_delivered;
        nfstime4       ffil_total_busy_time;
        nfstime4       ffil_aggregate_completion_time;
};

struct ff_layoutupdate4 {
        netaddr4       ffl_addr;
        nfs_fh4        ffl_fhandle;
        ff_io_latency4 ffl_read;
        ff_io_latency4 ffl_write;
        nfstime4       ffl_duration;
        bool           ffl_local;
};

struct io_info4 {
        uint32_t        ii_count;
        uint64_t        ii_bytes;
};

struct ff_iostats4 {
        offset4           ffis_offset;
        length4           ffis_length;
        stateid4          ffis_stateid;
        io_info4          ffis_read;
        io_info4          ffis_write;
        deviceid4         ffis_deviceid;
        ff_layoutupdate4  ffis_layoutupdate;
};

struct ff_layoutreturn4 {
        ff_ioerr4     fflr_ioerr_report<>;
        ff_iostats4   fflr_iostats_report<>;
};

union ff_mirrors_hint switch (bool ffmc_valid) {
    case TRUE:
        uint32_t    ffmc_mirrors;
    case FALSE:
        void;
};

struct ff_layouthint4 {
    ff_mirrors_hint fflh_mirrors_hint;
};

const RCA4_TYPE_MASK_FF_LAYOUT_MIN     = 16;
const RCA4_TYPE_MASK_FF_LAYOUT_MAX     = 17;

enum ff_cb_recall_any_mask {
    FF_RCA4_TYPE_MASK_READ = -2,
    FF_RCA4_TYPE_MASK_RW   = -1
};

