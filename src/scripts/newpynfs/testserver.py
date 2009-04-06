#!/usr/bin/env python
# nfs4stest.py - nfsv4 server tester
#
# Requires python 2.3
# 
# Written by Fred Isaman <iisaman@citi.umich.edu>
# Copyright (C) 2004 University of Michigan, Center for 
#                    Information Technology Integration
#
# Based on pynfs
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


import sys
if sys.hexversion < 0x02030000:
    print "Requires python 2.3 or higher"
    sys.exit(1)
import os
# Allow to be run stright from package root
if  __name__ == "__main__":
    if os.path.isfile(os.path.join(sys.path[0], 'lib', 'testmod.py')):
        sys.path.insert(1, os.path.join(sys.path[0], 'lib'))

import re
import testmod
from optparse import OptionParser, OptionGroup, IndentedHelpFormatter
import nfs4.servertests.environment as environment
import socket
import rpc
import cPickle as pickle

VERSION="0.2"

# Auth_sys defaults
HOST = socket.gethostname()
if not hasattr(os, "getuid"):
    UID = 0
else:
    UID = os.getuid()
if not hasattr(os, "getgid"):
    GID = 0
else:
    GID = os.getgid()


def parse_url(url):
    """Parse [nfs://]host:port/path"""
    p = re.compile(r"""
    (?:nfs://)?      # Ignore an optionally prepended 'nfs://'
    (?P<host>[^:]+)  # set host=everything up to next :
    :?
    (?P<port>[^/]*)  # set port=everything up to next /
    (?P<path>/.*$|$) # set path=everything else
    """, re.VERBOSE)

    m = p.match(url)
    if m:
        return m.group('host'), m.group('port'), m.group('path')
    else:
        return None, None, None
        
def unixpath2comps(str, pathcomps=None):
    if pathcomps is None or str[0] == '/':
        pathcomps = []
    else:
        pathcomps = pathcomps[:]
    for component in str.split('/'):
        if (component == '') or (component == '.'):
            pass
        elif component == '..':
            pathcomps = pathcomps[:-1]
        else:
            pathcomps.append(component)
    return pathcomps

def scan_options(p):
    """Parse command line options

    Sets the following:
    .showflags = (False)
    .showcodes = (False)
    .noinit    = (False)
    .nocleanup = (False)
    .outfile   = (None)
    .debug_fail = (False)
    
    .security = (sys)
    .uid = (UID)
    .gid = (GID)
    .machinename = (HOST)

    .force   = (False)
    .rundeps = (False)
    
    .verbose  = (False)
    .showpass = (True)
    .showwarn = (True)
    .showfail = (True)
    .showomit = (False)

    .maketree  = (False)
    .uselink   = (None)
    .useblock  = (None)
    .usechar   = (None)
    .usesocket = (None)
    .usefifo   = (None)
    .usefile   = (None)
    .usedir    = (None)
    .usespecial= (None)
    
    """
    p.add_option("--showflags", action="store_true", default=False,
                 help="Print a list of all possible flags and exit")
    p.add_option("--showcodes", action="store_true", default=False,
                 help="Print a list of all test codes and exit")
    p.add_option("--noinit", action="store_true", default=False,
                 help="Skip initial cleanup of test directory")
    p.add_option("--nocleanup", action="store_true", default=False,
                 help="Skip final cleanup of test directory")
    p.add_option("--outfile", "--out", default="out_last", metavar="FILE",
                 help="Store test results in FILE [out_last]")
    p.add_option("--debug_fail", action="store_true", default=False,
                 help="Force some checks to fail")

    g = OptionGroup(p, "Security flavor options",
                    "These options choose or affect the security flavor used.")
    g.add_option("--security", default='sys',
                 help="Choose security flavor such as krb5i [sys]")
    g.add_option("--uid", default=UID, type='int',
                 help="uid for auth_sys [%i]" % UID)
    g.add_option("--gid", default=GID, type='int',
                 help="gid for auth_sys [%i]" % GID)
    g.add_option("--machinename", default=HOST, metavar="HOST",
                 help="Machine name to use for auth_sys [%s]" % HOST)
    p.add_option_group(g)
    
    g = OptionGroup(p, "Test selection options",
                    "These options affect how flags are interpreted.")
    g.add_option("--force", action="store_true", default=False,
                 help="Force tests to be run, ignoring dependencies.")
    g.add_option("--rundeps", action="store_true", default=False,
                 help="Force test dependencies to be run, "
                 "even if not requested on command line")
    p.add_option_group(g)
    
    g = OptionGroup(p, "Test output options",
                    "These options affect how test results are shown")
    g.add_option("-v", "--verbose", action="store_true", default=False,
                  help="Show tests as they are being run")
    g.add_option("--showpass", action="store_true", default=True,
                 help="Show passed tests [default]")
    g.add_option("--hidepass", action="store_false", dest="showpass",
                 help="Hide passed tests")
    g.add_option("--showwarn", action="store_true", default=True,
                 help="Show tests that gave warnings [default]")
    g.add_option("--hidewarn", action="store_false", dest="showwarn",
                 help="Hide tests that gave warnings")
    g.add_option("--showfail", action="store_true", default=True,
                 help="Show failed tests [default]")
    g.add_option("--hidefail", action="store_false", dest="showfail",
                 help="Hide failed tests")
    g.add_option("--showomit", action="store_true", default=False,
                 help="Show omitted tests")
    g.add_option("--hideomit", action="store_false", dest="showomit",
                 help="Hide omitted tests [default]")
    p.add_option_group(g)

    g = OptionGroup(p, "Test tree options",
                    "If the tester cannot create various objects, certain "
                    "tests will not run.  You can indicate pre-existing "
                    "objects on the server which can be used "
                    "(they will not altered).")
    g.add_option("--maketree", action="store_true", default=False,
                 help="(Re)create the test tree of object types")
    g.add_option("--uselink", default=None, metavar="OBJPATH",
                 help="Use SERVER:/OBJPATH as symlink")
    g.add_option("--useblock", default=None, metavar="OBJPATH",
                 help="Use SERVER:/OBJPATH as block device")
    g.add_option("--usechar", default=None, metavar="OBJPATH",
                 help="Use SERVER:/OBJPATH as char device")
    g.add_option("--usesocket", default=None, metavar="OBJPATH",
                 help="Use SERVER:/OBJPATH as socket")
    g.add_option("--usefifo", default=None, metavar="OBJPATH",
                 help="Use SERVER:/OBJPATH as fifo")
    g.add_option("--usefile", default=None, metavar="OBJPATH",
                 help="Use SERVER:/OBJPATH as regular file")
    g.add_option("--usedir", default=None, metavar="OBJPATH",
                 help="Use SERVER:/OBJPATH as directory")
    g.add_option("--usespecial", default=None, metavar="OBJPATH",
                 help="Use SERVER:/OBJPATH as obj for certain specialized tests")
    p.add_option_group(g)

    g = OptionGroup(p, "Server workaround options",
                    "Certain servers handle certain things in unexpected ways."
                    " These options allow you to alter test behavior so that "
                    "they will run.")
    g.add_option("--paddednull", action="store_true", default=False,
                 help="Allow NULL returns to have extra data appended [False]")
    g.add_option("--newverf", action="store_true", default=False,
                 help="Force use of new verifier for SETCLIENTID [False]")
    p.add_option_group(g)
    return p.parse_args()

class Argtype(object):
    """Args that are not options are either flags or testcodes"""
    def __init__(self, obj, run=True, flag=True):
        self.isflag = flag  # True if flag, False if a test
        self.run = run      # True for inclusion, False for exclusion
        self.obj = obj      # The flag or test itself

    def __str__(self):
        return "Isflag=%i, run=%i" % (self.isflag, self.run)

def run_filter(test, options):
    """Determine whether a test was directly asked for by the command line."""
    run = False   # default
    for arg in options.args:
        if arg.isflag:
            if test.flags & arg.obj:
                run = arg.run
        else:
            if test == arg.obj:
                run = arg.run
    return run

def printflags(list):
    """Print all legal flag names, which are given in list"""
    from nfs4.nfs4_const import nfs_opnum4
    command_names = [s.lower()[3:].replace('_', '') \
                     for s in nfs_opnum4.values()]
    list.sort()
    # First print command names
    print
    for s in list:
        if s in command_names:
            print s
    # Then everything else
    print
    for s in list:
        if s not in command_names:
            print s
    
def main():
    p = OptionParser("%prog SERVER:/PATH [options] flags|testcodes\n"
                     "       %prog --help\n"
                     "       %prog SHOWOPTION",
                     version="%prog "+VERSION,
                     formatter=IndentedHelpFormatter(2, 25)
                     )
    opt, args = scan_options(p)

    # Create test database
    tests, fdict, cdict = testmod.createtests('nfs4.servertests')

    # Deal with any informational options
    if opt.showflags:
        printflags(fdict.keys())
        sys.exit(0)

    if opt.showcodes:
        codes = cdict.keys()
        codes.sort()
        for c in codes:
            print c
        sys.exit(0)

    # Grab server info and set defaults
    if not args:
        p.error("Need a server")
    url = args.pop(0)
    opt.server, opt.port, opt.path = parse_url(url)
    if not opt.server:
        p.error("%s not a valid server name" % url)
    if not opt.port:
        opt.port = 2049
    else:
        opt.port = int(opt.port)
    if not opt.path:
        opt.path = []
    else:
        opt.path = unixpath2comps(opt.path)

    # Check --use* options are valid
    for attr in dir(opt):
        if attr.startswith('use'):
            path = getattr(opt, attr)
            #print attr, path
            if path is None:
                path = opt.path + ['tree', attr[3:]]
            else:
                # FIXME - have funct that checks path validity
                if path[0] != '/':
                    p.error("Need to use absolute path for --%s" % attr)
                # print path
                if path[-1] == '/' and attr != 'usedir':
                    p.error("Can't use dir for --%s" %attr)
                try:
                    path = unixpath2comps(path)
                except Exception, e:
                    p.error(e)
            setattr(opt, attr, [comp for comp in path if comp])

    opt.path += ['tmp']

    # Check that --security option is valid
    # sets --flavor to a rpc.SecAuth* class, and sets flags for its options
    valid = rpc.supported.copy()
    # FIXME - STUB - the only gss mech available is krb5
    if 'gss' in valid:
        valid['krb5'] =  valid['krb5i'] =  valid['krb5p'] = valid['gss']
        del valid['gss']
    if opt.security not in valid:
        p.error("Unknown security: %s\nValid flavors are %s" %
                (opt.security, str(valid.keys())))
    opt.flavor = valid[opt.security]
    opt.service = {'krb5':1, 'krb5i':2, 'krb5p':3}.get(opt.security, 0)
               
    # Make sure args are valid
    opt.args = []
    for a in args:
        if a.lower().startswith('no'):
            include = False
            a = a[2:]
        else:
            include = True
        if a in fdict:
            opt.args.append(Argtype(fdict[a], include))
        elif a in cdict:
            opt.args.append(Argtype(cdict[a], include, flag=False))
        else:
            p.error("Unknown code or flag: %s" % a)

    # DEBUGGING
    environment.debug_fail = opt.debug_fail

    # Place tests in desired order
    tests.sort() # FIXME - add options for random sort

    # Run the tests and save/print results
    try:
        env = environment.Environment(opt)
        env.init()
    except Exception, e:
        print "Initialization failed, no tests run."
        if not opt.maketree:
            print "Perhaps you need to use the --maketree option"
        print sys.exc_info()[1]
        sys.exit(1)
    if opt.outfile is not None:
        fd = file(opt.outfile, 'w')
    try:
        clean_finish = False
        testmod.runtests(tests, opt, env, run_filter)
        clean_finish = True
    finally:
        if opt.outfile is not None:
            pickle.dump(tests, fd, 0)
        if not clean_finish:
            testmod.printresults(tests, opt)
    try:
        fail = False
        env.finish()
    except Exception, e:
        fail = True
    testmod.printresults(tests, opt)
    if fail:
        print "\nWARNING: could not clean testdir due to:\n%s\n" % str(e)

if __name__ == "__main__":
    main()
