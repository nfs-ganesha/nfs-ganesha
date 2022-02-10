/* SPDX-License-Identifier: unknown license... */
/*
 * Hand updated.
 * It was generated using rpcgen.
 */

#include "config.h"

#include "rquota.h"

bool xdr_sq_dqblk(XDR * xdrs, sq_dqblk * objp)
{
	register int32_t *buf;

	if (xdrs->x_op == XDR_ENCODE) {
		buf = xdr_inline_encode(xdrs, 8 * BYTES_PER_XDR_UNIT);
		if (buf != NULL) {
			/* most likely */
			IXDR_PUT_U_INT32(buf, objp->rq_bhardlimit);
			IXDR_PUT_U_INT32(buf, objp->rq_bsoftlimit);
			IXDR_PUT_U_INT32(buf, objp->rq_curblocks);
			IXDR_PUT_U_INT32(buf, objp->rq_fhardlimit);
			IXDR_PUT_U_INT32(buf, objp->rq_fsoftlimit);
			IXDR_PUT_U_INT32(buf, objp->rq_curfiles);
			IXDR_PUT_U_INT32(buf, objp->rq_btimeleft);
			IXDR_PUT_U_INT32(buf, objp->rq_ftimeleft);
		} else {
			if (!XDR_PUTUINT32(xdrs, objp->rq_bhardlimit))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_bsoftlimit))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_curblocks))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_fhardlimit))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_fsoftlimit))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_curfiles))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_btimeleft))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_ftimeleft))
				return false;
		}
		return true;
	}

	if (xdrs->x_op == XDR_DECODE) {
		buf = xdr_inline_decode(xdrs, 8 * BYTES_PER_XDR_UNIT);
		if (buf != NULL) {
			/* most likely */
			objp->rq_bhardlimit = IXDR_GET_U_INT32(buf);
			objp->rq_bsoftlimit = IXDR_GET_U_INT32(buf);
			objp->rq_curblocks = IXDR_GET_U_INT32(buf);
			objp->rq_fhardlimit = IXDR_GET_U_INT32(buf);
			objp->rq_fsoftlimit = IXDR_GET_U_INT32(buf);
			objp->rq_curfiles = IXDR_GET_U_INT32(buf);
			objp->rq_btimeleft = IXDR_GET_U_INT32(buf);
			objp->rq_ftimeleft = IXDR_GET_U_INT32(buf);
		} else {
			if (!XDR_GETUINT32(xdrs, &objp->rq_bhardlimit))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_bsoftlimit))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_curblocks))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_fhardlimit))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_fsoftlimit))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_curfiles))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_btimeleft))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_ftimeleft))
				return false;
		}
		return true;
	}

	if (!xdr_u_int(xdrs, &objp->rq_bhardlimit))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_bsoftlimit))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_curblocks))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_fhardlimit))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_fsoftlimit))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_curfiles))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_btimeleft))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_ftimeleft))
		return false;
	return true;
}

bool xdr_getquota_args(XDR * xdrs, getquota_args * objp)
{
	register __attribute__ ((__unused__)) int32_t *buf;

	if (!xdr_string(xdrs, &objp->gqa_pathp, RQ_PATHLEN))
		return false;
	if (!xdr_int(xdrs, &objp->gqa_uid))
		return false;
	return true;
}

bool xdr_setquota_args(XDR * xdrs, setquota_args * objp)
{
	register __attribute__ ((__unused__)) int32_t *buf;

	if (!xdr_int(xdrs, &objp->sqa_qcmd))
		return false;
	if (!xdr_string(xdrs, &objp->sqa_pathp, RQ_PATHLEN))
		return false;
	if (!xdr_int(xdrs, &objp->sqa_id))
		return false;
	if (!xdr_sq_dqblk(xdrs, &objp->sqa_dqblk))
		return false;
	return true;
}

bool xdr_ext_getquota_args(XDR * xdrs, ext_getquota_args * objp)
{
	register __attribute__ ((__unused__)) int32_t *buf;

	if (!xdr_string(xdrs, &objp->gqa_pathp, RQ_PATHLEN))
		return false;
	if (!xdr_int(xdrs, &objp->gqa_type))
		return false;
	if (!xdr_int(xdrs, &objp->gqa_id))
		return false;
	return true;
}

bool xdr_ext_setquota_args(XDR * xdrs, ext_setquota_args * objp)
{
	register __attribute__ ((__unused__)) int32_t *buf;

	if (!xdr_int(xdrs, &objp->sqa_qcmd))
		return false;
	if (!xdr_string(xdrs, &objp->sqa_pathp, RQ_PATHLEN))
		return false;
	if (!xdr_int(xdrs, &objp->sqa_id))
		return false;
	if (!xdr_int(xdrs, &objp->sqa_type))
		return false;
	if (!xdr_sq_dqblk(xdrs, &objp->sqa_dqblk))
		return false;
	return true;
}

bool xdr_rquota(XDR * xdrs, rquota * objp)
{
	register int32_t *buf;

	if (xdrs->x_op == XDR_ENCODE) {
		buf = xdr_inline_encode(xdrs, 10 * BYTES_PER_XDR_UNIT);
		if (buf != NULL) {
			/* most likely */
			IXDR_PUT_INT32(buf, objp->rq_bsize);
			IXDR_PUT_BOOL(buf, objp->rq_active);
			IXDR_PUT_U_INT32(buf, objp->rq_bhardlimit);
			IXDR_PUT_U_INT32(buf, objp->rq_bsoftlimit);
			IXDR_PUT_U_INT32(buf, objp->rq_curblocks);
			IXDR_PUT_U_INT32(buf, objp->rq_fhardlimit);
			IXDR_PUT_U_INT32(buf, objp->rq_fsoftlimit);
			IXDR_PUT_U_INT32(buf, objp->rq_curfiles);
			IXDR_PUT_U_INT32(buf, objp->rq_btimeleft);
			IXDR_PUT_U_INT32(buf, objp->rq_ftimeleft);
		} else {
			if (!XDR_PUTINT32(xdrs, objp->rq_bsize))
				return false;
			if (!XDR_PUTBOOL(xdrs, objp->rq_active))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_bhardlimit))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_bsoftlimit))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_curblocks))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_fhardlimit))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_fsoftlimit))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_curfiles))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_btimeleft))
				return false;
			if (!XDR_PUTUINT32(xdrs, objp->rq_ftimeleft))
				return false;
		}
		return true;
	}

	if (xdrs->x_op == XDR_DECODE) {
		buf = xdr_inline_decode(xdrs, 10 * BYTES_PER_XDR_UNIT);
		if (buf != NULL) {
			/* most likely */
			objp->rq_bsize = IXDR_GET_INT32(buf);
			objp->rq_active = IXDR_GET_BOOL(buf);
			objp->rq_bhardlimit = IXDR_GET_U_INT32(buf);
			objp->rq_bsoftlimit = IXDR_GET_U_INT32(buf);
			objp->rq_curblocks = IXDR_GET_U_INT32(buf);
			objp->rq_fhardlimit = IXDR_GET_U_INT32(buf);
			objp->rq_fsoftlimit = IXDR_GET_U_INT32(buf);
			objp->rq_curfiles = IXDR_GET_U_INT32(buf);
			objp->rq_btimeleft = IXDR_GET_U_INT32(buf);
			objp->rq_ftimeleft = IXDR_GET_U_INT32(buf);
		} else {
			if (!XDR_GETINT32(xdrs, &objp->rq_bsize))
				return false;
			if (!XDR_GETBOOL(xdrs, &objp->rq_active))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_bhardlimit))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_bsoftlimit))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_curblocks))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_fhardlimit))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_fsoftlimit))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_curfiles))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_btimeleft))
				return false;
			if (!XDR_GETUINT32(xdrs, &objp->rq_ftimeleft))
				return false;
		}
		return true;
	}

	if (!xdr_int(xdrs, &objp->rq_bsize))
		return false;
	if (!xdr_bool(xdrs, &objp->rq_active))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_bhardlimit))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_bsoftlimit))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_curblocks))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_fhardlimit))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_fsoftlimit))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_curfiles))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_btimeleft))
		return false;
	if (!xdr_u_int(xdrs, &objp->rq_ftimeleft))
		return false;
	return true;
}

bool xdr_qr_status(XDR * xdrs, qr_status * objp)
{
	register __attribute__ ((__unused__)) int32_t *buf;

	if (!xdr_enum(xdrs, (enum_t *) objp))
		return false;
	return true;
}

bool xdr_getquota_rslt(XDR * xdrs, getquota_rslt * objp)
{
	register __attribute__ ((__unused__)) int32_t *buf;

	if (!xdr_qr_status(xdrs, &objp->status))
		return false;
	switch (objp->status) {
	case Q_OK:
		if (!xdr_rquota(xdrs, &objp->getquota_rslt_u.gqr_rquota))
			return false;
		break;
	case Q_NOQUOTA:
		break;
	case Q_EPERM:
		break;
	default:
		return false;
	}
	return true;
}

bool xdr_setquota_rslt(XDR * xdrs, setquota_rslt * objp)
{
	register __attribute__ ((__unused__)) int32_t *buf;

	if (!xdr_qr_status(xdrs, &objp->status))
		return false;
	switch (objp->status) {
	case Q_OK:
		if (!xdr_rquota(xdrs, &objp->setquota_rslt_u.sqr_rquota))
			return false;
		break;
	case Q_NOQUOTA:
		break;
	case Q_EPERM:
		break;
	default:
		return false;
	}
	return true;
}
