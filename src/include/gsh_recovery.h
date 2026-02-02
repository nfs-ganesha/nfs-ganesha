/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright IBM Corporation, 2023
 *  Contributor: Frank Filz  <ffilzlnx@us.ibm.com>
 *
 * --------------------------
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *
 */

#ifndef GSH_RECOVERY_H
#define GSH_RECOVERY_H

enum recovery_backend {
	RECOVERY_BACKEND_FS,
	RECOVERY_BACKEND_FS_NG,
	RECOVERY_BACKEND_RADOS_KV,
	RECOVERY_BACKEND_RADOS_NG,
	RECOVERY_BACKEND_RADOS_CLUSTER,
	RECOVERY_BACKEND_NONE,
};

/**
 * @brief Default value of recovery_backend.
 */
#define RECOVERY_BACKEND_DEFAULT RECOVERY_BACKEND_FS

/*******************************************************************************
 *
 * RECOVERY ENTRY FORMATS
 *
 * All current recovery databases use the same format for the actual recovery
 * entry. The Rados backends also are keyed by clientid4 value, but that is
 * separate.
 *
 * Length Delimited String (LSTR{}) Format (for use below):
 *
 * len:string
 * %d:%s
 *
 * NFSv4 clid Name Format:
 *
 *    <IP>-(LSTR{cliid})
 *    "%s-(%d:%s)"
 *
 * NSM State Format:
 *
 *    NSM_STATE-state
 *    NSM_STATE-%d
 *
 * NLM Client Format:
 *
 *    NLM-<CLIENT-IP>-<SERV-IP>-(netid)(LSTR{caller-name})
 *    "NLM-%s-%s-(%s)(%d:%s)"
 *
 ******************************************************************************/

#endif /* GSH_RECOVERY_H */
