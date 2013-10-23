#ifndef DBUS_PRIV_H
#define DBUS_PRIV_H

bool dbus_proc_property(const char *method, DBusMessage *msg,
			DBusMessage *reply, DBusError *error,
			struct gsh_dbus_interface **interfaces);

int dbus_append_signal_string(DBusMessageIter *args, void *sig_string);

int dbus_send_signal(DBusConnection *conn, char *obj_name, char *int_name,
		     char *sig_name, int (*payload) (DBusMessageIter *signal,
						     void *args),
		     void *sig_args);

#endif				/* DBUS_PRIV_H */
