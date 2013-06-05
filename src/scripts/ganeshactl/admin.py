from PyQt4 import QtCore, QtDBus

class AdminInterface(QtDBus.QDBusAbstractInterface):
    '''
    org.ganesha.nfsd.admin interface
    '''
    def __init__(self, service, path, connection, show_status, parent=None):
        super(AdminInterface, self).__init__(service,
                                             path,
                                             'org.ganesha.nfsd.admin',
                                             connection,
                                             parent)
        self.show_status = show_status

    def grace(self, ipaddr):
        async = self.asyncCall("grace", ipaddr)
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.admin_done)

    def reload(self):
        async = self.asyncCall("reload")
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.admin_done)

    def shutdown(self):
        async = self.asyncCall("shutdown")
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.admin_done)

    # catch the reply and forward it to the UI
    def admin_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "DBUS error:" + str(reply.error().message()))
        else:
            status = reply.argumentAt(0).toPyObject()
            msg = reply.argumentAt(1).toPyObject()
            self.show_status.emit(status, msg)

