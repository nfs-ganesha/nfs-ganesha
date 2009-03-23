#!/usr/bin/env python2

DESCRIPTION = """\
pynfs is a collection of tools and libraries for NFS4. It includes
a NFS4 library and a server test application.
"""

import sys
from distutils.core import setup, Extension
from distutils.dep_util import newer_group
import os
import glob

topdir = os.getcwd()
if  __name__ == "__main__":
    if os.path.isfile(os.path.join(topdir, 'lib', 'testmod.py')):
        sys.path.insert(1, os.path.join(topdir, 'lib'))

import rpcgen

def needs_updating(xdrfile):
    gen_path = os.path.join(topdir, 'lib', 'rpcgen.py')
    name_base = xdrfile[:xdrfile.rfind(".")]
    sources = [gen_path, xdrfile]
    targets = [ name_base + "_const.py",
                name_base + "_type.py",
                name_base + "_pack.py" ]
    for t in targets:
        if newer_group(sources, t):
            return True
    return False

def use_xdr(dir, xdrfile):
    """Move to dir, and generate files based on xdr file"""
    os.chdir(dir)
    if needs_updating(xdrfile):
        rpcgen.run(xdrfile)
        for file in glob.glob(os.path.join(dir, 'parse*')):
            print "deleting", file
            os.remove(file)

def generate_files():
    home = os.getcwd()
    dir = os.path.join(topdir, 'lib', 'nfs4')
    use_xdr(dir, 'nfs4.x')
    import ops_gen # this must be delayed until nfs4.x is parsed
    sources = [ os.path.join(topdir, 'lib', 'ops_gen.py'),
                'nfs4_const.py', 'nfs4_type.py' ]
    if newer_group(sources, 'nfs4_ops.py'):
        print "Generating nfs4_ops.py"
        ops_gen.run()
    dir = os.path.join(topdir, 'lib', 'rpc')
    use_xdr(dir, 'rpc.x')
    dir = os.path.join(topdir, 'lib', 'rpc', 'rpcsec')
    use_xdr(dir, 'gss.x')
    os.chdir(home)

# FRED - figure how to get this to run only with build/install type command
generate_files()

# Describes how to compile gssapi.so - change this as needed
# FRED - is there a general way to look this info up?
# FRED - for use out of package root, is there a way to place compiled library with source?

if os.path.exists('/usr/include/heimdal'):
    # This works with Heimdal kerberos (Suse)
    gssapi = Extension('rpc.rpcsec.gssapi',
                       extra_compile_args = ['-Wall'],
                       define_macros = [('HEIMDAL',1)],
                       include_dirs = ['/usr/kerberos/include',
                                       '/usr/include/heimdal'],
                       libraries = ['gssapi'],
                       library_dirs = ['/usr/kerberos/lib'],
                       sources = ['lib/rpc/rpcsec/gssapimodule.c'])
else:
    # This works with MIT kerberos (Fedora)
    gssapi = Extension('rpc.rpcsec.gssapi',
                       extra_compile_args = ['-Wall'],
                       include_dirs = ['/usr/kerberos/include'],
                       libraries = ['gssapi_krb5'],
                       library_dirs = ['/usr/kerberos/lib'],
                       sources = ['lib/rpc/rpcsec/gssapimodule.c'])

from testserver import VERSION
setup(name = "newpynfs",
      version = VERSION,
      license = "GPL",
      description = "Python NFS4 tools",
      long_description = DESCRIPTION,
      author = "Fred Isaman",
      author_email = "iisaman@citi.umich.edu",
      maintainer = "Fred Isaman",
      maintainer_email = "iisaman@citi.umich.edu",

      ext_modules = [gssapi],
      package_dir = {'': 'lib'},
      packages = ['nfs4', 'nfs4.servertests', 'ply', 'rpc', 'rpc.rpcsec'],
      py_modules = ['testmod', 'rpcgen'],
      scripts = ['testserver.py', 'showresults.py']
      )

PATHHELP = """\

See http://www.python.org/doc/current/inst/search-path.html for detailed
information on various ways to set the search path.
One easy way is to set the environment variable PYTHONPATH.
"""
if "install" in sys.argv:
    print PATHHELP
