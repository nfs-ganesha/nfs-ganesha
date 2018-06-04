// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2018 Red Hat, Inc.
 * Contributor : Frank Filz <ffilzlnx@mindspring.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#include "gtest.hh"

extern "C" {
/* Ganesha headers */
#include "nfs_lib.h"
#include "nfs_file_handle.h"
#include "nfs_proto_functions.h"
}

#ifndef GTEST_GTEST_NFS4_HH
#define GTEST_GTEST_NFS4_HH

namespace gtest {

  class GaeshaNFS4BaseTest : public gtest::GaneshaFSALBaseTest {
  protected:

    virtual void SetUp() {
      gtest::GaneshaFSALBaseTest::SetUp();

      memset(&data, 0, sizeof(struct compound_data));
      memset(&arg, 0, sizeof(nfs_arg_t));
      memset(&resp, 0, sizeof(struct nfs_resop4));

      ops = (struct nfs_argop4 *) gsh_calloc(1, sizeof(struct nfs_argop4));
      arg.arg_compound4.argarray.argarray_len = 1;
      arg.arg_compound4.argarray.argarray_val = ops;

      /* Setup some basic stuff (that will be overrode) so TearDown works. */
      data.minorversion = 0;
      ops[0].argop = NFS4_OP_PUTROOTFH;
    }

    virtual void TearDown() {
      bool rc;

      set_current_entry(&data, nullptr);

      nfs4_Compound_FreeOne(&resp);

      /* Free the compound data and response */
      compound_data_Free(&data);

      /* Free the args structure. */
      rc = xdr_free((xdrproc_t) xdr_COMPOUND4args, &arg);
      EXPECT_EQ(rc, true);

      gtest::GaneshaFSALBaseTest::TearDown();
    }

    void setCurrentFH(struct fsal_obj_handle *entry) {
      bool fhres;

      /* Convert root_obj to a file handle in the args */
      fhres = nfs4_FSALToFhandle(data.currentFH.nfs_fh4_val == NULL,
                                 &data.currentFH, entry, op_ctx->ctx_export);
      EXPECT_EQ(fhres, true);

      set_current_entry(&data, entry);
    }

    void setSavedFH(struct fsal_obj_handle *entry) {
      bool fhres;

      /* Convert root_obj to a file handle in the args */
      fhres = nfs4_FSALToFhandle(data.savedFH.nfs_fh4_val == NULL,
                                 &data.savedFH, entry, op_ctx->ctx_export);
      EXPECT_EQ(fhres, true);

      set_saved_entry(&data, entry);
    }

    void set_saved_export(void) {
      /* Set saved export from op_ctx */
      if (data.saved_export != NULL)
        put_gsh_export(data.saved_export);
      /* Save the export information and take reference. */
      get_gsh_export_ref(op_ctx->ctx_export);
      data.saved_export = op_ctx->ctx_export;
      data.saved_export_perms = *op_ctx->export_perms;
    }

    void setup_lookup(int pos, const char *name) {
      gsh_free(ops[pos].nfs_argop4_u.oplookup.objname.utf8string_val);
      ops[pos].argop = NFS4_OP_LOOKUP;
      ops[pos].nfs_argop4_u.oplookup.objname.utf8string_len = strlen(name);
      ops[pos].nfs_argop4_u.oplookup.objname.utf8string_val = gsh_strdup(name);
    }

    void cleanup_lookup(int pos) {
      gsh_free(ops[pos].nfs_argop4_u.oplookup.objname.utf8string_val);
      ops[pos].nfs_argop4_u.oplookup.objname.utf8string_len = 0;
      ops[pos].nfs_argop4_u.oplookup.objname.utf8string_val = nullptr;
    }

    void setup_putfh(int pos, struct fsal_obj_handle *entry) {
      bool fhres;

      gsh_free(ops[pos].nfs_argop4_u.opputfh.object.nfs_fh4_val);

      ops[pos].argop = NFS4_OP_PUTFH;

      /* Convert root_obj to a file handle in the args */
      fhres = nfs4_FSALToFhandle(true, &ops[pos].nfs_argop4_u.opputfh.object,
                                 entry, op_ctx->ctx_export);
      EXPECT_EQ(fhres, true);
    }

    void cleanup_putfh(int pos) {
      gsh_free(ops[pos].nfs_argop4_u.opputfh.object.nfs_fh4_val);
      ops[pos].nfs_argop4_u.opputfh.object.nfs_fh4_len = 0;
      ops[pos].nfs_argop4_u.opputfh.object.nfs_fh4_val = nullptr;
    }

    void setup_rename(int pos, const char *oldname, const char *newname) {
      gsh_free(ops[pos].nfs_argop4_u.oprename.oldname.utf8string_val);
      gsh_free(ops[pos].nfs_argop4_u.oprename.newname.utf8string_val);
      ops[pos].argop = NFS4_OP_RENAME;
      ops[pos].nfs_argop4_u.oprename.oldname.utf8string_len = strlen(oldname);
      ops[pos].nfs_argop4_u.oprename.oldname.utf8string_val =
                                                          gsh_strdup(oldname);
      ops[pos].nfs_argop4_u.oprename.newname.utf8string_len = strlen(newname);
      ops[pos].nfs_argop4_u.oprename.newname.utf8string_val =
                                                          gsh_strdup(newname);
    }

    void swap_rename(int pos) {
    	component4 temp = ops[pos].nfs_argop4_u.oprename.newname;
    	ops[pos].nfs_argop4_u.oprename.newname =
    	        ops[pos].nfs_argop4_u.oprename.oldname;
    	ops[pos].nfs_argop4_u.oprename.oldname = temp;
    }

    void cleanup_rename(int pos) {
      gsh_free(ops[pos].nfs_argop4_u.oprename.oldname.utf8string_val);
      gsh_free(ops[pos].nfs_argop4_u.oprename.newname.utf8string_val);
      ops[pos].nfs_argop4_u.oprename.oldname.utf8string_len = 0;
      ops[pos].nfs_argop4_u.oprename.oldname.utf8string_val = nullptr;
      ops[pos].nfs_argop4_u.oprename.newname.utf8string_len = 0;
      ops[pos].nfs_argop4_u.oprename.newname.utf8string_val = nullptr;
    }

    void setup_link(int pos, const char *newname) {
      gsh_free(ops[pos].nfs_argop4_u.oplink.newname.utf8string_val);
      ops[pos].argop = NFS4_OP_LINK;
      ops[pos].nfs_argop4_u.oplink.newname.utf8string_len = strlen(newname);
      ops[pos].nfs_argop4_u.oplink.newname.utf8string_val =
                                                          gsh_strdup(newname);
    }

    void cleanup_link(int pos) {
      gsh_free(ops[pos].nfs_argop4_u.oplink.newname.utf8string_val);
      ops[pos].nfs_argop4_u.oplink.newname.utf8string_len = 0;
      ops[pos].nfs_argop4_u.oplink.newname.utf8string_val = nullptr;
    }

    struct compound_data data;
    struct nfs_argop4 *ops;
    nfs_arg_t arg;
    struct nfs_resop4 resp;
  };
} // namespase gtest

#endif /* GTEST_GTEST_NFS4_HH */
