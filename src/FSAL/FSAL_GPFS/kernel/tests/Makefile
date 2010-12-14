
all: append_handle_prg create_handle_prg hardlink_handle_prg \
	 read_handle_prg readlink_handle_prg \
	 truncate_handle_prg create_handle_from_fd_prg

clean:
	$(RM) append_handle_prg create_handle_prg hardlink_handle_prg \
	 read_handle_prg readlink_handle_prg \
	 truncate_handle_prg create_handle_from_fd_prg

distdir:

distclean: clean

%: %.c
	$(CC) -o $@ $^
