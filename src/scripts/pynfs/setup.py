#!/usr/bin/env python2

DESCRIPTION = """\
pynfs is a collection of tools and libraries for NFS4. It includes a NFS4 library,
a commandline client and a server test application
"""

from distutils.core import setup

setup(name = "pynfs",
      version = "0.1",
      license = "GPL",
      description = "Python NFS4 tools",
      long_description = DESCRIPTION,
      author = "Peter Åstrand",
      author_email = "peter@cendio.se",
      url = "http://www.cendio.se/~peter/pynfs/",
      py_modules = ["nfs4constants",
                    "nfs4packer",
                    "nfs4types",
                    "rpc",
                    "nfs4lib",
                    "pynfs_completer"],
      scripts = ["rpcgen.py",
                 "epinfo2sxw.py",
                 "nfs4client.py",
                 "nfs4st.py",
                 "test_tree.py"])
