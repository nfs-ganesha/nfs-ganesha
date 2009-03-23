# testmod.py - run tests from a suite
#
# Requires python 2.3
# 
# Written by Fred Isaman <iisaman@citi.umich.edu>
# Copyright (C) 2004 University of Michigan, Center for 
#                    Information Technology Integration
#
import nfs4.nfs4lib
import re
import sys
from traceback import format_exception

if 'sum' not in __builtins__:
    def sum(seq, start=0):
        return reduce(lambda x,y: x+y, seq, start)

# Possible outcomes
TEST_NOTRUN  = 0    # Not yet considered
TEST_RUNNING = 1    # Test actually running
TEST_WAIT    = 2    # Waiting for dependencies to run
TEST_OMIT    = 3    # Test skipped
TEST_FAIL    = 4    # Test failed
TEST_NOTSUP  = 5    # Counts as WARN, but considered a failed dependency
TEST_WARN    = 6    # Technically a PASS, but there was a better way
TEST_PASS    = 7    # Test passed
DEP_FUNCT    = 100  # Used for depency functions

class Result(object):
    outcome_names = { TEST_NOTRUN : "NOT RUN",
                      TEST_RUNNING: "RUNNING",
                      TEST_WAIT   : "WAITING TO RUN",
                      TEST_OMIT   : "OMIT",
                      TEST_FAIL   : "FAILURE",
                      TEST_NOTSUP : "UNSUPPORTED",
                      TEST_WARN   : "WARNING",
                      TEST_PASS   : "PASS",
                      DEP_FUNCT   : "DEPENDENCY FUNCTION"
                      }

    def __init__(self, outcome=TEST_NOTRUN, msg="", tb=None, default=False):
        self.outcome = outcome
        self.msg = str(msg)
        self.default = default
        if tb is None:
            self.tb = []
        else:
            #self.tb = ''.join(format_exception(*tb))
            self.tb = format_exception(*tb)

    def __str__(self):
        return self.outcome_names[self.outcome]

    def __repr__(self):
        return '\n'.join([self.__str__(), self.msg])

    def __eq__(self, other):
        if type(other) == type(0):
            return self.outcome == other
        else:
            return id(self) == id(other)

    def __ne__(self, other):
        if type(other) == type(0):
            return self.outcome != other
        else:
            return id(self) != id(other)

class TestException(Exception):
    pass

class UnsupportedException(TestException):
    def __init__(self, *args):
        self.type = TEST_NOTSUP
        TestException.__init__(self, *args)

class FailureException(TestException):
    def __init__(self, *args):
        self.type = TEST_FAIL
        TestException.__init__(self, *args)

class WarningException(TestException):
    def __init__(self, *args):
        self.type = TEST_WARN
        TestException.__init__(self, *args)

class Test(object):
    _keywords = ["FLAGS", "DEPEND", "CODE"]
    _pass_result = Result(TEST_PASS, default=True)
    _run_result = Result(TEST_RUNNING, default=True)
    _wait_result = Result(TEST_WAIT, "Circular dependency", default=True)
    _omit_result = Result(TEST_OMIT, "Failed runfilter", default=True)
    _funct_result = Result(DEP_FUNCT, default=True)
    __re = re.compile(r'(\D*)(\d*)(.*)')

    def __init__(self, function, module=""):
        """Needs function to be run"""
        self.runtest = function
        self.name = function.__name__
        if module:
            self.fullname = module.split('.')[-1] + '.' + self.name
        else:
            self.fullname = self.name
        self.doc = function.__doc__.split('\n')[0].strip()
        #self.doc = function.__doc__.strip()
        self.result = Result()
        self._read_docstr(function.__doc__)

    def _read_docstr(self, s):
        """Searches s for 'keyword: list' and stores resulting lists"""
        for key in self._keywords:
            p = re.compile(r'^\s*' + key +':(.*$)', re.MULTILINE)
            match = p.search(str(s))
            if match is None:
                setattr(self, key.lower() + '_list', [])
            else:
                setattr(self, key.lower() + '_list', match.group(1).split())

    def __getstate__(self):
        """Remove function reference when pickling

        This vastly reduce size of the output file, while at the same
        time making it more robust to function/class renames.  However,
        if we need to restore this info for some reason, will need a
        __setstate__ function to try and recover it.
        """
        d = self.__dict__.copy()
        del d["runtest"]
        del d["dependencies"]
        del d["flags"]
        return d

##     def __cmp__(self, other):
##         if self.code < other.code:
##             return -1
##         elif self.code == other.code:
##             return 0
##         else:
##             return 1

    def __cmp__(self, other):
        me = self.__re.match(self.code)
        me = (me.group(1), int(me.group(2).zfill(1)), me.group(3))
        you = self.__re.match(other.code)
        you = (you.group(1), int(you.group(2).zfill(1)), you.group(3))
        if me < you:
            return -1
        elif me == you:
            return 0
        else:
            return 1

    def __str__(self):
        return "%-8s %s" % ( self.code, self.fullname)

    def __repr__(self):
        if self.result.msg:
            return "%-65s : %s\n%s" % (self, self.result, self._format(self.result.msg))
        else:
            return "%-65s : %s" % (self, self.result)

    def display(self, showdoc=False, showtrace=False):
        out = "%-65s : %s" % (str(self), str(self.result))
        if showdoc:
            out += "\n%s" % self._format(self.doc, 5, 70)
        if showtrace and self.result.tb:
            out += "\n%s" % ''.join(self.result.tb)
        elif self.result.msg:
            out += "\n%s" % self._format(self.result.msg, 11, 64)
        return out


    def _format(self, s, start_col=11, end_col=64):
        s = str(s)
        indent = ' ' * (start_col - 1)
        out = indent
        lout = len(out)
        words = s.split()
        for w in words:
            lw = len(w)
            if lout + lw > end_col and lout > start_col:
                out += '\n' + indent
                lout = start_col - 1
            out += ' ' + w
            lout += lw + 1
        return out
            
    def fail(self, msg):
        raise FailureException(msg)

    def fail_support(self, msg):
        raise UnsupportedException(msg)

    def pass_warn(self, msg):
        raise WarningException(msg)

    def __info(self):
        #return sys.exc_info()
        exctype, excvalue, tb = sys.exc_info()
        if sys.platform[:4] == 'java': ## tracebacks look different in Jython
            return (exctype, excvalue, tb)
        newtb = tb.tb_next
        if newtb is None:
            return (exctype, excvalue, tb)
        return (exctype, excvalue, newtb)

    def run(self, environment, verbose=False):
        """Run self.runtest, storing result"""
        #print "*********Running test %s (%s)" % (self.name, self.code)
        self.result = self._run_result
        if verbose:
            print repr(self)
        try:
            environment.startUp()
            self.runtest(self, environment)
            self.result = self._pass_result
        except KeyboardInterrupt:
            raise
        except TestException, e:
            self.result = Result(e.type, e, sys.exc_info())
        except StandardError, e:
            self.result = Result(TEST_FAIL, '', sys.exc_info())
            self.result.msg = self.result.tb[-1]
        except Exception, e:
            self.result = Result(TEST_FAIL, e, sys.exc_info())
        try:
            environment.shutDown()
        except StandardError, e:
            self.result = Result(TEST_FAIL, '', sys.exc_info())
            self.result.msg = self.result.tb[-1]
        if verbose:
            print repr(self)

class Environment(object):
    """Base class for a test environment"""
    def __init__(self, opts):
        self.opts = opts

    def init(self):
        """Run once before any test is run"""
        pass

    def finish(self):
        """Run once after all tests are run"""
        pass

    def startUp(self):
        """Run before each test"""
        pass

    def shutDown(self):
        """Run after each test"""
        pass
        
def _run_filter(test, options):
    """Returns True if test should be run, False if it should be skipped"""
    return True

def runtests(tests, options, environment, runfilter=_run_filter):
    """tests is an array of test objects, to be run in order
    
    (as much as possible)
    """
    for t in tests:
        if t.result == TEST_NOTRUN:
            _runtree(t, options, environment, runfilter)
        else:
            # Test has already been run in a dependency tree
            pass

def _runtree(t, options, environment, runfilter=_run_filter):
    if t.result == TEST_WAIT:
        return
    t.result = t._wait_result
    if not runfilter(t, options):
        # Check flags to see if test should be omitted
        t.result = t._omit_result
        return
    if options.rundeps:
        runfilter = lambda x, y : True
    for dep in t.dependencies:
        if dep.result == DEP_FUNCT:
            if (not options.force) and (not dep(t, environment)):
                t.result = Result(TEST_OMIT, 
                                  "Dependency function %s failed" %
                                  dep.__name__)
                return
            continue
        if dep.result == t._omit_result and options.rundeps:
            _runtree(dep, options, environment, runfilter)
        elif dep.result in [TEST_NOTRUN, TEST_WAIT]:
            _runtree(dep, options, environment, runfilter)
            # Note dep.result has now changed
        if dep.result == TEST_WAIT:
            return
        elif (not options.force) and \
                 (dep.result in [TEST_OMIT, TEST_FAIL, TEST_NOTSUP]):
            t.result = Result(TEST_OMIT, 
                              "Dependency %s had status %s." % \
                              (dep, dep.result))
            return
    t.run(environment, getattr(options, 'verbose', False))

def _import_by_name(name):
    mod = __import__(name)
    components = name.split('.')
    for comp in components[1:]:
        mod = getattr(mod, comp)
    return mod

def createtests(testdir):
    """ Tests are functions that start with "test".  Their docstring must
    contain a line starting with "FLAGS:" and a line starting with "CODE:".
    It may optionall contain a line starting with "DEPEND:".  Each test must
    have a unique code string.  The space seperated list of flags must
    not contain any code names.  The depend list is a list of code names for
    tests that must be run before the given test.
    """
    # Find all tests in testdir
    tests = []
    package = _import_by_name(testdir)
    for testfile in package.__all__:
        if testfile.endswith('.py'):
            testfile = testfile[:-3]
        testmod = ".".join([testdir, testfile])
        mod = _import_by_name(testmod)
        for attr in dir(mod):
            if attr.startswith("test"):
                f = getattr(mod, attr)
                tests.append(Test(f, testmod))
    # Reduce doc string info into format easier to work with
    used_codes = {}
    flag_dict = {}
    bit = 1L
    for t in tests:
        if not t.flags_list:
            raise("%s has no flags" % t.fullname)
        for f in t.flags_list:
            if f not in flag_dict:
                flag_dict[f] = bit
                bit <<= 1
        if len(t.code_list) != 1:
            raise("%s needs exactly one code" % t.fullname)
        t.code = t.code_list[0]
        if t.code in used_codes:
            raise("%s trying to use a code already used"  % t.fullname)
        used_codes[t.code] = t
        del t.code_list
    # Check that flags don't contain a code word
    for f in flag_dict:
        if f in used_codes:
            raise("flag %s is also used as a test code" % f)
    # Now turn dependency names into pointers, and flags into a bitmask
    for t in tests:
        t.flags = sum([flag_dict[x] for x in t.flags_list])
        t.dependencies = []
        for d in t.depend_list:
            if d in used_codes:
                t.dependencies.append(used_codes[d])
            else:
                mod = _import_by_name(t.runtest.__module__)
                if not hasattr(mod, d):
                    raise("Could not find reference to dependency %s" % str(d))
                funct = getattr(mod, d)
                if not callable(funct):
                    raise("Dependency %s of %s does not exist" %
                          (d, t.fullname))
                funct.result = t._funct_result
                t.dependencies.append(funct)
    return tests, flag_dict, used_codes

                 
def printresults(tests, opts, file=None):
    NOTRUN, OMIT, SKIP, FAIL, WARN, PASS = range(6)
    count = [0] * 6
    for t in tests:
        if not hasattr(t, "result"):
            print dir(t)
            print t.__dict__
            raise
        if t.result == TEST_NOTRUN:
            count[NOTRUN] += 1
        elif t.result == TEST_OMIT and t.result.default:
            count[OMIT] += 1
        elif t.result in [TEST_WAIT, TEST_OMIT]:
            count[SKIP] += 1
        elif t.result == TEST_FAIL:
            count[FAIL] += 1
        elif t.result in [TEST_NOTSUP, TEST_WARN]:
            count[WARN] += 1
        elif t.result == TEST_PASS:
            count[PASS] += 1
    print >> file, "*"*50 
    for t in tests:
        if t.result == TEST_NOTRUN:
            continue
        if t.result == TEST_OMIT and t.result.default:
            continue
        if (not opts.showomit) and t.result == TEST_OMIT:
            continue
        if (not opts.showpass) and t.result == TEST_PASS:
            continue
        if (not opts.showwarn) and t.result in [TEST_NOTSUP, TEST_WARN]:
            continue
        if (not opts.showfail) and t.result == TEST_FAIL:
            continue
        print >> file, t.display(0,0)
    print >> file, "*"*50
    if count[NOTRUN]:
        print >> file, "Tests interrupted! Only %i tests run" % \
              sum(count[SKIP:])
    else:
        print >> file, "Command line asked for %i of %i tests" % \
              (sum(count[SKIP:]), len(tests))
    print >> file, "Of those: %i Skipped, %i Failed, %i Warned, %i Passed" % \
          (count[SKIP], count[FAIL], count[WARN], count[PASS])
