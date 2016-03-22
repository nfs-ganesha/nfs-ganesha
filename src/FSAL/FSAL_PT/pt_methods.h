/* PT methods for handles
 */

#ifndef PT_METHODS_H
#define PT_METHODS_H

/* PT is effectively a single filesystem, describe it and assign all
 * PT handles to it.
 */
struct fsal_filesystem pt_filesystem;

/* method proto linkage to handle.c for export
 */

fsal_status_t pt_lookup_path(struct fsal_export *exp_hdl,
			     const char *path, struct fsal_obj_handle **handle);

fsal_status_t pt_create_handle(struct fsal_export *exp_hdl,
			       struct gsh_buffdesc *hdl_desc,
			       struct fsal_obj_handle **handle);

void pt_handle_ops_init(struct fsal_obj_ops *ops);

/*
 * PT internal object handle
 * handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 * wrt locks, should this be a lock counter??
 * AF_UNIX sockets are strange ducks.  I personally cannot see why they
 * are here except for the ability of a client to see such an animal with
 * an 'ls' or get rid of one with an 'rm'.  You can't open them in the
 * usual file way so open_by_handle_at leads to a deadend.  To work around
 * this, we save the args that were used to mknod or lookup the socket.
 */

struct pt_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct attrlist attributes;
	ptfsal_handle_t *handle;
	union {
		struct {
			int fd;
			fsal_openflags_t openflags;
		} file;
		struct {
			unsigned char *link_content;
			int link_size;
		} symlink;
		struct {
			struct pt_file_handle *dir;
			char *name;
		} unopenable;
	} u;
};

static inline bool pt_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
	    || (type == BLOCK_FILE)) {
		return true;
	} else {
		return false;
	}
}

	/* I/O management */
fsal_status_t pt_open(struct fsal_obj_handle *obj_hdl,
		      fsal_openflags_t openflags);
fsal_openflags_t pt_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t pt_read(struct fsal_obj_handle *obj_hdl,
		      uint64_t offset,
		      size_t buffer_size, void *buffer, size_t *read_amount,
		      bool *end_of_file);

fsal_status_t pt_write(struct fsal_obj_handle *obj_hdl,
		       uint64_t offset,
		       size_t buffer_size, void *buffer, size_t *wrote_amount,
		       bool *fsal_stable);

/* sync */
fsal_status_t pt_commit(struct fsal_obj_handle *obj_hdl,
			off_t offset, size_t len);

fsal_status_t pt_close(struct fsal_obj_handle *obj_hdl);

#endif				/* PT_METHODS_H */
