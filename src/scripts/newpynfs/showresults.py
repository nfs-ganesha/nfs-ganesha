#!/usr/bin/env python
# showresults.py - redisplay results from nfsv4 server tester output file
#
# Requires python 2.3
# 
# Written by Fred Isaman <iisaman@citi.umich.edu>
# Copyright (C) 2004 University of Michigan, Center for 
#                    Information Technology Integration
#


# Allow to be run stright from package root
if  __name__ == "__main__":
    import os.path
    import sys
    if os.path.isfile(os.path.join(sys.path[0], 'lib', 'testmod.py')):
        sys.path.insert(1, os.path.join(sys.path[0], 'lib'))

#import cPickle as pickle
import pickle
import testmod
from optparse import OptionParser

class MyUnpickler(pickle.Unpickler):
    class Unknown(object):
        def __init__(self, name):
            self.name = name
    
    def find_class(self, module, name):
        # Handle function renames gracefully
        __import__(module)
        mod = sys.modules[module]
        try:
            klass = getattr(mod, name)
            return klass
        except:
            return self.Unknown(name)

def show(filename, opt):
    fd = file(filename, 'r')
    p = MyUnpickler(fd)
    tests = p.load()
    testmod.printresults(tests, opt)

def scan_options(p):
    """Parse command line options"""
    p.add_option("--showpass", action="store_true", default=True,
                 help="Show passed tests [default]")
    p.add_option("--hidepass", action="store_false", dest="showpass",
                 help="Hide passed tests")
    p.add_option("--showomit", action="store_true", default=False,
                 help="Show omitted tests")
    p.add_option("--hideomit", action="store_false", dest="showomit",
                 help="Hide omitted tests [default]")
    p.add_option("--showwarn", action="store_true", default=True,
                 help="Show tests that gave warnings [default]")
    p.add_option("--hidewarn", action="store_false", dest="showwarn",
                 help="Hide tests that gave warnings")
    p.add_option("--showfail", action="store_true", default=True,
                 help="Show failed tests [default]")
    p.add_option("--hidefail", action="store_false", dest="showfail",
                 help="Hide failed tests")
    return p.parse_args()

def main():
    p = OptionParser("%prog [options] filename")
    opt, args = scan_options(p)
    if not args:
        p.error("Need a filename")
    for a in args:
        show(a, opt)

if __name__ == "__main__":
    main()
