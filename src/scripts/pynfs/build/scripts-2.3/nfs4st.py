#!/usr/bin/python2

# nfs4st.py - NFS4 server tester
#
# Written by Peter Åstrand <peter@cendio.se>
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
#
# Extend unittest with warnings.
#
# Handle errors such as NFS4ERR_RESOURCE and NFS4ERR_DELAY.
#
# filehandles are split into eq. classes "valid filehandle" and
# "invalid filehandle". There should probably be a class "no filehandle" as
# well. Currently, "invalid filehandle" are tested by doing operations without
# filehandles.
#
# Add testing of \ to testSlash methods.
#
# More testing of strange attributes and handling of NFS4ERR_ATTRNOTSUPP;
# more fine-grained eqv.part of attribute masks. 
#
# Nomenclature: Each test class is referred to as a "test suite". Each
# test* method is a "test case".

import nfs4st
from nfs4st import *
from nfs4st.base_st_classes import *

#
# nfs4st test execution code
#

class MyTextTestResult(unittest._TextTestResult):
	def __init__(self, print_tracebacks, stream, descriptions, verbosity):
		unittest._TextTestResult.__init__(self, stream, descriptions, verbosity)
		if not print_tracebacks:
			self.printErrors = lambda: 0

	def getID(self, test):
		id = test.id()
		if id.startswith("__main__"):
			return id[9:]
		else:
			return id
    
	def startTest(self, test):
		unittest.TestResult.startTest(self, test)
		if self.showAll:
			self.stream.write(self.getID(test) + ":\n")
			self.stream.write(" " + self.getDescription(test))
			self.stream.write(" ... ")

class MyTextTestRunner(unittest.TextTestRunner):
	def __init__(self, print_tracebacks, stream=None, descriptions=None, verbosity=None):
		kwargs = {}
		if stream: kwargs["stream"] = stream
		if descriptions: kwargs["descriptions"] = descriptions
		if verbosity: kwargs["verbosity"] = verbosity
		unittest.TextTestRunner.__init__(self, **kwargs)
		self.print_tracebacks = print_tracebacks
    
	def _makeResult(self):
		ttr = MyTextTestResult(self.print_tracebacks, self.stream,
				       self.descriptions, self.verbosity)
		return ttr
    

class TestProgram(unittest.TestProgram):
	USAGE = """\
Usage: %(progName)s [nfs://]host[:port]<prefix> [options] [test] [...]

<prefix> defaults to /. Use same prefix as for test_tree_net.py 

Options:
  -u, --udp        use UDP as transport (default)
  -t, --tcp        use TCP as transport
  -h, --help       Show this message
  -q, --quiet      Minimal output
  -v, --verbose    Verbose output, display tracebacks

Examples:
  %(progName)s                               - run default set of tests
  %(progName)s MyTestSuite                   - run suite 'MyTestSuite'
  %(progName)s MyTestSuite.testSomething     - run MyTestCase.testSomething
"""
	def parseArgs(self, argv):
		import getopt
		import re
        
		self.verbosity = 2
		self.print_tracebacks = 0
		self.testNames = []

		# Reorder arguments, so we can add options at the end 
		ordered_args = []
		for arg in argv[1:]:
			if arg.startswith("-"):
				ordered_args.insert(0, arg)
			else:
				ordered_args.append(arg)
        
		try:
			options, args = getopt.getopt(ordered_args, 'uthqv',
						      ['help', 'quiet', 'udp', 'tcp', 'verbose'])
		except getopt.error, msg:
			self.usageExit(msg)
            
		for opt, value in options:
			if opt in ("-u", "--udp"):
				base_st_classes.transport = "udp"
			if opt in ("-t", "--tcp"):
				base_st_classes.transport = "tcp"
			if opt in ('-h','--help'):
				self.usageExit()
			if opt in ('-q','--quiet'):
				self.verbosity = 0
			if opt in ('-v','--verbose'):
				self.print_tracebacks = 1

		if len(args) < 1:
			self.usageExit()

		parse_result = nfs4lib.parse_nfs_url(args[0])
		if not parse_result:
			self.usageExit()

		(nfs4st.base_st_classes.host, portstring, directory) = parse_result

		if not directory:
			directory = "/"
            
		base_st_classes.prefix = os.path.join(directory, "nfs4st_tree")

		if portstring:
			base_st_classes.port = int(portstring)
		else:
			base_st_classes.port = nfs4lib.NFS_PORT

		args = args[1:]

		# Runs all test suites if no arguments are passed.
		if len(args) == 0 and self.defaultTest is None:
			global_list = globals().keys()
			for suite in global_list:
				if re.match("st_", suite) != None:
					self.testNames.append(suite)

		elif len(args) > 0:
			self.testNames = args
		else:
			self.testNames = (self.defaultTest,)

		self.createTests()

	def runTests(self):
		self.testRunner = MyTextTestRunner(self.print_tracebacks, verbosity=self.verbosity)
		result = self.testRunner.run(self.test)
		sys.exit(not result.wasSuccessful())


main = TestProgram

if __name__ == "__main__":
	main()
