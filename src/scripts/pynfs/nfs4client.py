#!/usr/bin/env python2

# nfs4client.py - NFS4 client. 
#
# Written by Peter Astrand <peter@cendio.se>
# Copyright (C) 2001 Cendio Systems AB (http://www.cendio.se)
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License. 
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# TODO:
# completion: should not complete commands as arguments.
# Handle errors such as NFS4ERR_RESOURCE and NFS4ERR_DELAY.
# Move LOOKUP handling to common function.
# State handling is broken. 

from nfs4constants import *
from nfs4types import *
import nfs4lib
import cmd
import sys
import getopt
import re
import os
import time
import string

USAGE = """\
Usage: %s [nfs://]host[:[port]]<directory> [-u|-t] [-d debuglevel]
       [-c string] 
options:
-h, --help                   display this help and exit
-u, --udp                    use UDP as transport
-t, --tcp                    use TCP as transport
-d level, --debuglevel level set debuglevel
-p, --pythonmode             enable Python interpreter mode
-c, --commandstring string   execute semicolon separated commands
    --uid=<uid>              Use custom <uid> with AUTH_UNIX security
    --gid=<gid>              Use custom <gid> with AUTH_UNIX security

Default transport is to try TCP first, then UDP.
nfs4client uses the RPC security flavor AUTH_UNIX. 
"""

VERSION = "0.0"
BUFSIZE = 4096

# Load readline & completer
try:
    import readline
    import pynfs_completer
except ImportError:
    print "Module readline not available. Tab-completion disabled."
    class FakeCompleter:
        def __init__(self):
            self.pythonmode = 0
    Completer = FakeCompleter
else:
    # Readline is available
    import __builtin__
    import __main__
    class RealCompleter(pynfs_completer.Completer):
        def __init__(self):
            self.pythonmode = 0
            readline.set_completer(self.complete)
            pynfs_completer.set_history_file(".nfs4client")
        
        commands = [
            "help", "cd", "rm", "dir", "ls", "exit", "quit", "get",
            "put", "mkdir", "md", "rmdir", "rd", "cat", "page",
            "debuglevel", "ping", "version", "pythonmode", "shell", "access",
            "create", "getattr", "readlink", "requestlock", "getfacl",
	    "unlock" ]

        def complete(self, text, state):
            """Return the next possible completion for 'text'.

            This is called successively with state == 0, 1, 2, ... until it
            returns None.  The completion should begin with 'text'.

            """
            if state == 0:
                if "." in text and self.pythonmode:
                    self.matches = self.attr_matches(text)
                else:
                    self.matches = self.global_matches(text)
            try:
                return self.matches[state]
            except IndexError:
                return None

            
        def global_matches(self, text):
            """Compute matches when text is a simple name.

            Return a list of all keywords, built-in functions and names
            currently defines in __main__ that match.

            """
            import keyword
            matches = []
            n = len(text)

            searchlist = [Completer.commands]
            if self.pythonmode:
                searchlist.append(keyword.kwlist)
                searchlist.append(__builtin__.__dict__.keys())
                searchlist.append(__main__.__dict__.keys())

            for list in searchlist:
                for word in list:
                    if word[:n] == text and word != "__builtins__":
                        matches.append(word)

            return matches
    Completer = RealCompleter

class ClientApp(cmd.Cmd):
    def __init__(self, ncl, directory, pythonmode, debuglevel):
        cmd.Cmd.__init__(self)
        self.ncl = ncl
        
        self.completer = Completer()
        self.completer.pythonmode = pythonmode

        self.debuglevel = debuglevel
        
        self.baseprompt = "nfs4: %s>"
        self._connect()
        self._set_prompt()

        # Try to CD to specified directory
        self.do_cd(directory)

	self.locks = {}
	self.seqid = 1

    def _connect(self):
        # Use debugtags?
        self.ncl.debugtags = (self.debuglevel and 1)
        self.ncl.init_connection()

    def _set_prompt(self):
        self.prompt = self.baseprompt % nfs4lib.comps2unixpath(self.ncl.cwd)
	
    def add_lock(self, fh, lock_data):
	if self.locks.has_key(fh):
	    self.locks[fh].append(lock_data)
	else:
	    self.locks[fh] = [lock_data]

    def release_lock(self, fh, lock_data):
	self.locks[fh].remove(lock_data)
	if self.locks[fh] == []:
	    del self.locks[fh]

    #
    # Commands
    #
   
    def do_access(self, line):
        if not line:
            print "access <filename>"
            return

        allrights = ACCESS4_DELETE + ACCESS4_EXECUTE + ACCESS4_EXTEND + ACCESS4_LOOKUP \
                    + ACCESS4_MODIFY + ACCESS4_READ
        
        pathcomps = self.ncl.get_pathcomps_rel(line)
        putrootfhop = self.ncl.putrootfh_op()
        lookupops = self.ncl.lookup_path(pathcomps)
        operations = [putrootfhop] + lookupops
        
        # ACCESS
        operations.append(self.ncl.access_op(allrights))
        res = self.ncl.compound(operations)
        try:
            nfs4lib.check_result(res)
        except nfs4lib.BadCompoundRes, r:
            print "access failed:", r
            return

        access = res.resarray[-1].arm.arm.access

        def is_allowed(access, bit):
            if access & bit:
                return "allowed"
            else:
                return "not allowed"

        print "ACCESS4_READ is", is_allowed(access, ACCESS4_READ)
        print "ACCESS4_LOOKUP is", is_allowed(access, ACCESS4_LOOKUP)
        print "ACCESS4_MODIFY is", is_allowed(access, ACCESS4_MODIFY)
        print "ACCESS4_EXTEND is", is_allowed(access, ACCESS4_EXTEND)
        print "ACCESS4_DELETE is", is_allowed(access, ACCESS4_DELETE)
        print "ACCESS4_EXECUTE is", is_allowed(access, ACCESS4_EXECUTE)

    def do_cat(self, line):
        filenames = line.split()

        if not filenames:
            print "cat <filename>..."
            return
        
        for file in filenames:
            f = nfs4lib.NFS4OpenFile(self.ncl)
            try:
                f.open(file)
                print f.read(),
                f.close()
            except nfs4lib.BadCompoundRes, r:
                print "Error fetching file:", r
        print

    def do_cd(self, line):
        if not line:
            print "cd <directory>"
            return
        
        if line == "..":
            self.ncl.cd_dotdot()
        elif line == ".":
            return
        else:
            try:
                self.ncl.try_cd(line)
            except nfs4lib.ChDirError, e:
                print e

        self._set_prompt()

    def do_create(self, line):
        args = line.split()
        if len(args) < 2:
            print "create <type> <name> <arguments>"
            return

        (type, objname) = line.split(None, 3)[:2]
        if type == "link":
            if len(args) < 3:
                print "create link <name> <target>"
                return
            else:
                linkdata = args[2]
            objtype = createtype4(self.ncl, type=NF4LNK, linkdata=linkdata)
        elif type == "block":
            if len(args) < 4:
                print "create block <name> major minor"
                return
            major = int(args[2])
            minor = int(args[3])
            devdata = specdata4(self.ncl, major, minor)
            objtype = createtype4(self.ncl, type=NF4BLK, devdata=devdata)
        elif type == "char":
            if len(args) < 4:
                print "create char <name> major minor"
                return
            major = int(args[2])
            minor = int(args[3])
            devdata = specdata4(self.ncl, major, minor)
            objtype = createtype4(self.ncl, type=NF4CHR, devdata=devdata)
        elif type == "socket":
            objtype = createtype4(self.ncl, type=NF4SOCK)
        elif type == "fifo":
            objtype = createtype4(self.ncl, type=NF4FIFO)
        elif type == "dir":
            objtype = createtype4(self.ncl, type=NF4DIR)
        else:
            print "unknown type"
            return

        pathcomps = self.ncl.get_pathcomps_rel(objname)
        dircomps = pathcomps[:-1]
        lookupops = self.ncl.lookup_path(dircomps)
        operations = [self.ncl.putrootfh_op()] + lookupops

        # CREATE
        createop = self.ncl.create(objtype, pathcomps[-1])
        operations.append(createop)

        try:
            res = self.ncl.compound(operations)
            nfs4lib.check_result(res)
        except nfs4lib.BadCompoundRes, r:
            print "create failed:", r
            return
    
    def do_debuglevel(self, line):
        if not line:
            print "debuglevel is", self.debuglevel
        else:
            try:
                l = int(line)
            except:
                print "Invalid debuglevel"
                return
            self.debuglevel = l
            self.ncl.debugtags = (self.debuglevel and 1)

    def do_dir(self, line):
        pathcomps = self.ncl.get_pathcomps_rel(line)
        putrootfhop = self.ncl.putrootfh_op()
        lookupops = self.ncl.lookup_path(pathcomps)
        operations = [putrootfhop] + lookupops

        getfhop = self.ncl.getfh_op()
        operations.append(getfhop)

        res = self.ncl.compound(operations)
        try:
            nfs4lib.check_result(res)
        except nfs4lib.BadCompoundRes, r:
            print "Cannot list directory:", r
            return
            
        getfhresult = res.resarray[-1].arm
        fh = getfhresult.arm.object

        attr_request = nfs4lib.list2attrmask([FATTR4_TYPE, FATTR4_SIZE, FATTR4_TIME_MODIFY])
        entries = self.ncl.do_readdir(fh, attr_request)
        for entry in entries:
            attrdict = nfs4lib.fattr2dict(entry.attrs)
            # Name
            name = entry.name

            # Size
            if attrdict.has_key("size"):
                size = str(attrdict["size"])
            else:
                size = "?"

            # File type
            if attrdict.has_key("type"):
                ftype = nfs_ftype4_id[attrdict["type"]]
                if ftype == "NF4REG":
                    ftype = ""
                else:
                    # Skip NF4
                    ftype = ftype[3:]
            else:
                ftype = "?"
            
            # Time
            if attrdict.has_key("time_modify"):
                time_tupel = time.localtime(attrdict["time_modify"].seconds)
                # ISO 8601 format without timezone. 
                time_mod = time.strftime("%Y-%m-%dT%H:%M:%S", time_tupel)
            else:
                time_mod = "?"
                
            print "%(name)-16s %(size)12s %(ftype)9s %(time_mod)34s" % vars()

    def do_EOF(self, unused_line):
        print
        sys.exit(0)

    def do_get(self, line):
        filenames = line.split()

        if not filenames:
            print "get <filename>..."
            return
        
        for file in filenames:
            basename = os.path.basename(file)
            remote = nfs4lib.NFS4OpenFile(self.ncl)
            try:
                remote.open(file)
                local = open(basename, "w")
                while 1:
                    # Read large chunks
                    data = remote.read(BUFSIZE*64)
                    if not data:
                        break
                    
                    local.write(data)
                
                remote.close()
                local.close()
            except nfs4lib.BadCompoundRes, r:
                print "Error fetching file:", r
        print

    def do_getattr(self, line):
        """Get regular attributes"""
  
        argv = line.split()
        object = argv[0]
        attributes = argv[1:]
        
        attrlist = []
        dict = nfs4lib.get_attrbitnum_dict()
        for attr in attributes:
                if attr.isdigit() == 1:
                        attrlist.append(attr.atoi())
                elif dict.has_key(attr):
                        attrlist.append(dict[attr])
        
        pathcomps = self.ncl.get_pathcomps_rel(object)
        lookupops = self.ncl.lookup_path(pathcomps)
        operations = [self.ncl.putrootfh_op()] + lookupops
        operations.append( self.ncl.getattr_op(nfs4lib.list2attrmask(attrlist)))
        try:
                res = self.ncl.compound(operations)
        except nfs4lib.BadCompoundRes, result:
                print "getattr failed: ", result
                return
        if res.status <> NFS4_OK:
                print "getattr failed, returned error: %s" % (nfsstat4_id[res.status])
                return
        getattrres = res.resarray[-1].opgetattr.resok4.obj_attributes
        dict = nfs4lib.fattr2dict(getattrres)
        for attr in dict.keys():
                print "%s: %s" % (attr, dict[attr])
        return

    def do_getfacl(self, line):
	"""Get regular attributes"""
 	argv = line.split()
 	object = argv[0]
 	attributes = argv[1:]
 	
 	attrlist = [ 12 ]
 	
 	pathcomps = self.ncl.get_pathcomps_rel(object)
 	lookupops = self.ncl.lookup_path(pathcomps)
 	operations = [self.ncl.putrootfh_op()] + lookupops
 	operations.append(self.ncl.getattr_op(nfs4lib.list2attrmask(attrlist)))
 	try:
	    res = self.ncl.compound(operations)
 	except nfs4lib.BadCompoundRes, result:
	    print "getattr failed: ", result
	    return
 	if res.status <> NFS4_OK:
	    print "getattr failed, returned error: %s" % (nfsstat4_id[res.status])
	    return
 	getattrres = res.resarray[-1].opgetattr.resok4.obj_attributes
 	dict = nfs4lib.fattr2dict(getattrres)
 	for attr in dict.keys():
	    print "%s: %s" % (attr, dict[attr])
 	return

    def do_mkdir(self, line):
        if not line:
            print "mkdir <dirname>"
            return

        self.do_create("dir " + line)

    def do_ping(self, unused_line):
        print "pinging", self.ncl.host, "via RPC NULL procedure"
        start = time.time()
        self.ncl.null()
        end = time.time()
        print self.ncl.host, "responded in %f seconds" % (end - start)

    def do_put(self, line):
        fields = line.split()
        
        if len(fields) < 1 or len(fields) > 2:
            print "put <local name> [remote name]"
            return

        local_name = fields[0]
        try:
            remote_name = fields[1]
        except IndexError:
            remote_name = os.path.basename(local_name)
        
        remote = nfs4lib.NFS4OpenFile(self.ncl)
        try:
            local = open(local_name)
            remote.open(remote_name, "w")
	    print "Files Opened."
            while 1:
                data = local.read(BUFSIZE*64)
                if not data:
                    break
		print "Writing."
                remote.write(data)

	    remote.close()
            local.close()
        except nfs4lib.BadCompoundRes, r:
            print "Error fetching file:", r
	print

    def do_pythonmode(self, unused_line):
        self.completer.pythonmode = (not self.completer.pythonmode)
        print "pythonmode is now",
        if self.completer.pythonmode:
            print "on"
        else:
            print "off"

    def do_readlink(self, line):
        pathcomps = self.ncl.get_pathcomps_rel(line)
        putrootfhop = self.ncl.putrootfh_op()
        lookupops = self.ncl.lookup_path(pathcomps)
        operations = [putrootfhop] + lookupops
        
        readlinkop = self.ncl.readlink_op()
        operations.append(readlinkop)

        res = self.ncl.compound(operations)
        try:
	    nfs4lib.check_result(res)
	except nfs4lib.BadCompoundRes, r:
	    print "Error reading link data:", r
	    return

	print "Link points to:", res.resarray[-1].arm.arm.link

    # FIXME: What do seqid's and stateid's really have to do with it?
    def do_requestlock(self, line):
	pathcomps = self.ncl.get_pathcomps_rel(line)
	putrootfhop = self.ncl.putrootfh_op()
	lookupops = self.ncl.lookup_path(pathcomps)
	operations = [putrootfhop] + lookupops

	getfhop = self.ncl.getfh_op()
	operations.append(getfhop)

	stateid = stateid4(ncl, self.seqid, "FIXME")
	lock_type = READ_LT
	offset = 0
	length = pow(2, 64) - 1

	lock_owner = lock_owner4(ncl, ncl.clientid, "FIXME")
	open_to_lock_owner = open_to_lock_owner4(ncl, self.seqid, stateid, self.seqid, lock_owner)
	locker_me = locker4(ncl, TRUE, open_to_lock_owner)
	lockop = self.ncl.lock_op(lock_type, FALSE, offset,
				  length, locker=locker_me)
	operations.append(lockop)

	res = self.ncl.compound(operations)
	self.seqid = self.seqid + 1
	try:
	    nfs4lib.check_result(res)
	except nfs4lib.BadCompoundRes, r:
	    print "Error retrieving lock response:", r
	    return

	# If operation returned NFS4_OK:
	if not res.status:
	    fh = res.resarray[-2].arm.arm.object
	    lock_data = nfs4lib.LockData(offset, length, lock_type, stateid)
	    self.add_lock(fh, lock_data)

    def do_rm(self, line):
        """Remove non-dir object"""

        # Make sure the object to remove is not a directory
        pathcomps = self.ncl.get_pathcomps_rel(line)
        ftype = self.ncl.get_ftype(pathcomps)
        if ftype == NF4DIR:
            print "%s is a directory (try rmdir instead)" % line
            return

        dircomps = pathcomps[:-1]
        lookupops = self.ncl.lookup_path(dircomps)
        operations = [self.ncl.putrootfh_op()] + lookupops

        objname = pathcomps[-1]
        operations.append(self.ncl.remove_op(objname))

        try:
            res = self.ncl.compound(operations)
            nfs4lib.check_result(res)
        except nfs4lib.BadCompoundRes, r:
            print "remove failed:", r
            return

    def do_rmdir(self, line):
        """Remove directory"""

        # Make sure the object to remove is a directory
        pathcomps = self.ncl.get_pathcomps_rel(line)
        ftype = self.ncl.get_ftype(pathcomps)
        if ftype != NF4DIR:
            print "%s is not a directory (try rm instead)" % line
            return

        dircomps = pathcomps[:-1]
        lookupops = self.ncl.lookup_path(dircomps)
        operations = [self.ncl.putrootfh_op()] + lookupops

        objname = pathcomps[-1]
        operations.append(self.ncl.remove_op(objname))

        try:
            res = self.ncl.compound(operations)
            nfs4lib.check_result(res)
        except nfs4lib.BadCompoundRes, r:
            print "remove failed:", r
            return

    def do_shell(self, line):
        os.system(line)

    def do_unlock(self, line):
	pathcomps = self.ncl.get_pathcomps_rel(line)
	putrootfhop = self.ncl.putrootfh_op()
	lookupops = self.ncl.lookup_path(pathcomps)
	operations = [putrootfhop] + lookupops

	getfhop = self.ncl.getfh_op()
	operations.append(getfhop)

	res = self.ncl.compound(operations)
	try:
	    nfs4lib.check_result(res)
	except nfs4lib.BadCompoundRes, r:
	    print "Error getting filehandle:", r
	    return

	fh = res.resarray[-1].arm.arm.object

	if not self.locks.has_key(fh):
	    print "Lock not found in client-side locking table."
	    return

	putfhop = self.ncl.putfh_op(fh)
	lock_data = self.locks[fh][0]
	unlockop = self.ncl.locku_op(lock_data.locktype, lock_data.stateid.seqid,
				     lock_data.stateid, lock_data.offset,
				     lock_data.length)
	operations = [putfhop, unlockop]

	res = self.ncl.compound(operations)
	try:
	    nfs4lib.check_result(res)
	except nfs4lib.BadCompoundRes, r:
	    print "Error unlocking file:", r
	    return

	# Delete lock from local lock table
	self.release_lock(fh, lock_data)

    def do_version(self, unused_line):
        print "nfs4client.py version", VERSION

    do_ls = do_dir

    do_exit = do_EOF

    do_quit = do_EOF

    do_md = do_mkdir

    do_rd = do_rmdir

    do_page = do_cat


    #
    # Misc. 
    #
    def help_cd(self):
        print "Change current directory"
    
    def help_overview(self):
        print USAGE % sys.argv[0]

    def emptyline(self):
        pass

    def default(self, line):
        if line == "xyzzy":
            print "Beware of black rabbits!"
            return
        
        if not self.completer.pythonmode:
            print "Unknown command", line
            return
        
        if line[:1] == '@':
            line = line[1:]

        try:
            code = compile(line + '\n', '<stdin>', 'single')
            exec code in globals()
        except:
            import traceback
            traceback.print_exc()


def usage():
    print >> sys.stderr, USAGE % sys.argv[0]
    sys.exit(2)


# FIXME: Remove if/when Python library supports GNU style scanning. 
def my_getopt(args, shortopts, longopts = []):
    opts = []
    prog_args = []
    if type(longopts) == type(""):
        longopts = [longopts]
    else:
        longopts = list(longopts)

    # Allow options after non-option arguments?
    if shortopts[0] == '+':
        shortopts = shortopts[1:]
        all_options_first = 1
    elif os.environ.has_key("POSIXLY_CORRECT"):
        all_options_first = 1
    else:
        all_options_first = 0

    while args:
        if args[0] == '--':
            prog_args += args[1:]
            break

        if args[0][:2] == '--':
            opts, args = getopt.do_longs(opts, args[0][2:], longopts, args[1:])
        elif args[0][:1] == '-':
            opts, args = getopt.do_shorts(opts, args[0][1:], shortopts, args[1:])
        else:
            if all_options_first:
                prog_args += args
                break
            else:
                prog_args.append(args[0])
                args = args[1:]

    return opts, prog_args


if __name__ == "__main__":
    if len(sys.argv) < 2:
        usage()

    # Let getopt parse the arguments
    try:
        opts, args = my_getopt(sys.argv[1:], "hutd:pc:",
                               ["help", "udp", "tcp", "debuglevel=",
                                "pythonmode", "commandstring=",
                                "uid=", "gid="])
    except getopt.GetoptError, e:
        print >> sys.stderr, e
        usage()
        sys.exit(2)

    transport = "auto"
    debuglevel = 0
    pythonmode = 0
    commandstring = None
    kwargs = {}

    for o, a in opts:
        if o in ("-h", "--help"):
            usage()
            sys.exit()
        if o in ("-u", "--udp"):
            transport = "udp"
        if o in ("-t", "--tcp"):
            transport = "tcp"
        if o in ("-d", "--debuglevel"):
            try:
                debuglevel = int(a)
            except:
                print "Invalid debuglevel"
                sys.exit()
        if o in ("-p", "--pythonmode"):
            pythonmode = 1
        if o in ("-c", "--commandstring"):
            commandstring = a
        if o == "--uid":
            kwargs["uid"] = int(a)
        if o == "--gid":
            kwargs["gid"] = int(a)

    # By now, there should only be one argument left.
    if len(args) != 1:
        print >> sys.stderr, "the number of non-option arguments is not one"
        usage()
    else:
        parse_result = nfs4lib.parse_nfs_url(args[0])
        if not parse_result:
            usage()

        (host, portstring, directory) = parse_result

        if portstring:
            port = int(portstring)
        else:
            port = nfs4lib.NFS_PORT

        if not directory:
            directory = "/"

    ncl = nfs4lib.create_client(host, port, transport, **kwargs)
    c = ClientApp(ncl, directory, pythonmode, debuglevel)

    commands = []
    if commandstring:
        commands = commandstring.split(";")

    for command in commands:
        try:
            c.onecmd(command)
        except nfs4lib.BadCompoundRes, e:
            print e
            
    while 1:
        # do_* methods should preferrable handle errors themself. They can provide
        # much better error messages. This try-clause is a last resort. 
        try:
            c.cmdloop()
        except nfs4lib.BadCompoundRes, e:
            print e

    
# Local variables:
# py-indent-offset: 4
# tab-width: 8
# End:
