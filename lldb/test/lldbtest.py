"""
LLDB module which provides the abstract base class of lldb test case.

The concrete subclass can override lldbtest.TesBase in order to inherit the
common behavior for unitest.TestCase.setUp/tearDown implemented in this file.

The subclass should override the attribute mydir in order for the python runtime
to locate the individual test cases when running as part of a large test suite
or when running each test case as a separate python invocation.

./dotest.py provides a test driver which sets up the environment to run the
entire of part of the test suite .  Example:

# Exercises the test suite in the types directory....
/Volumes/data/lldb/svn/ToT/test $ ./dotest.py -A x86_64 types
...

Session logs for test failures/errors/unexpected successes will go into directory '2012-05-16-13_35_42'
Command invoked: python ./dotest.py -A x86_64 types
compilers=['clang']

Configuration: arch=x86_64 compiler=clang
----------------------------------------------------------------------
Collected 72 tests

........................................................................
----------------------------------------------------------------------
Ran 72 tests in 135.468s

OK
$ 
"""

import os, sys, traceback
import os.path
import re
import signal
from subprocess import *
import StringIO
import time
import types
import unittest2
import lldb

# See also dotest.parseOptionsAndInitTestdirs(), where the environment variables
# LLDB_COMMAND_TRACE and LLDB_DO_CLEANUP are set from '-t' and '-r dir' options.

# By default, traceAlways is False.
if "LLDB_COMMAND_TRACE" in os.environ and os.environ["LLDB_COMMAND_TRACE"]=="YES":
    traceAlways = True
else:
    traceAlways = False

# By default, doCleanup is True.
if "LLDB_DO_CLEANUP" in os.environ and os.environ["LLDB_DO_CLEANUP"]=="NO":
    doCleanup = False
else:
    doCleanup = True


#
# Some commonly used assert messages.
#

COMMAND_FAILED_AS_EXPECTED = "Command has failed as expected"

CURRENT_EXECUTABLE_SET = "Current executable set successfully"

PROCESS_IS_VALID = "Process is valid"

PROCESS_KILLED = "Process is killed successfully"

PROCESS_EXITED = "Process exited successfully"

PROCESS_STOPPED = "Process status should be stopped"

RUN_SUCCEEDED = "Process is launched successfully"

RUN_COMPLETED = "Process exited successfully"

BACKTRACE_DISPLAYED_CORRECTLY = "Backtrace displayed correctly"

BREAKPOINT_CREATED = "Breakpoint created successfully"

BREAKPOINT_STATE_CORRECT = "Breakpoint state is correct"

BREAKPOINT_PENDING_CREATED = "Pending breakpoint created successfully"

BREAKPOINT_HIT_ONCE = "Breakpoint resolved with hit cout = 1"

BREAKPOINT_HIT_TWICE = "Breakpoint resolved with hit cout = 2"

BREAKPOINT_HIT_THRICE = "Breakpoint resolved with hit cout = 3"

MISSING_EXPECTED_REGISTERS = "At least one expected register is unavailable."

OBJECT_PRINTED_CORRECTLY = "Object printed correctly"

SOURCE_DISPLAYED_CORRECTLY = "Source code displayed correctly"

STEP_OUT_SUCCEEDED = "Thread step-out succeeded"

STOPPED_DUE_TO_EXC_BAD_ACCESS = "Process should be stopped due to bad access exception"

STOPPED_DUE_TO_ASSERT = "Process should be stopped due to an assertion"

STOPPED_DUE_TO_BREAKPOINT = "Process should be stopped due to breakpoint"

STOPPED_DUE_TO_BREAKPOINT_WITH_STOP_REASON_AS = "%s, %s" % (
    STOPPED_DUE_TO_BREAKPOINT, "instead, the actual stop reason is: '%s'")

STOPPED_DUE_TO_BREAKPOINT_CONDITION = "Stopped due to breakpoint condition"

STOPPED_DUE_TO_BREAKPOINT_IGNORE_COUNT = "Stopped due to breakpoint and ignore count"

STOPPED_DUE_TO_SIGNAL = "Process state is stopped due to signal"

STOPPED_DUE_TO_STEP_IN = "Process state is stopped due to step in"

STOPPED_DUE_TO_WATCHPOINT = "Process should be stopped due to watchpoint"

DATA_TYPES_DISPLAYED_CORRECTLY = "Data type(s) displayed correctly"

VALID_BREAKPOINT = "Got a valid breakpoint"

VALID_BREAKPOINT_LOCATION = "Got a valid breakpoint location"

VALID_COMMAND_INTERPRETER = "Got a valid command interpreter"

VALID_FILESPEC = "Got a valid filespec"

VALID_MODULE = "Got a valid module"

VALID_PROCESS = "Got a valid process"

VALID_SYMBOL = "Got a valid symbol"

VALID_TARGET = "Got a valid target"

VALID_PLATFORM = "Got a valid platform"

VALID_TYPE = "Got a valid type"

VALID_VARIABLE = "Got a valid variable"

VARIABLES_DISPLAYED_CORRECTLY = "Variable(s) displayed correctly"

WATCHPOINT_CREATED = "Watchpoint created successfully"

def CMD_MSG(str):
    '''A generic "Command '%s' returns successfully" message generator.'''
    return "Command '%s' returns successfully" % str

def COMPLETION_MSG(str_before, str_after):
    '''A generic message generator for the completion mechanism.'''
    return "'%s' successfully completes to '%s'" % (str_before, str_after)

def EXP_MSG(str, exe):
    '''A generic "'%s' returns expected result" message generator if exe.
    Otherwise, it generates "'%s' matches expected result" message.'''
    return "'%s' %s expected result" % (str, 'returns' if exe else 'matches')

def SETTING_MSG(setting):
    '''A generic "Value of setting '%s' is correct" message generator.'''
    return "Value of setting '%s' is correct" % setting

def EnvArray():
    """Returns an env variable array from the os.environ map object."""
    return map(lambda k,v: k+"="+v, os.environ.keys(), os.environ.values())

def line_number(filename, string_to_match):
    """Helper function to return the line number of the first matched string."""
    with open(filename, 'r') as f:
        for i, line in enumerate(f):
            if line.find(string_to_match) != -1:
                # Found our match.
                return i+1
    raise Exception("Unable to find '%s' within file %s" % (string_to_match, filename))

def pointer_size():
    """Return the pointer size of the host system."""
    import ctypes
    a_pointer = ctypes.c_void_p(0xffff)
    return 8 * ctypes.sizeof(a_pointer)

def is_exe(fpath):
    """Returns true if fpath is an executable."""
    return os.path.isfile(fpath) and os.access(fpath, os.X_OK)

def which(program):
    """Returns the full path to a program; None otherwise."""
    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None

class recording(StringIO.StringIO):
    """
    A nice little context manager for recording the debugger interactions into
    our session object.  If trace flag is ON, it also emits the interactions
    into the stderr.
    """
    def __init__(self, test, trace):
        """Create a StringIO instance; record the session obj and trace flag."""
        StringIO.StringIO.__init__(self)
        # The test might not have undergone the 'setUp(self)' phase yet, so that
        # the attribute 'session' might not even exist yet.
        self.session = getattr(test, "session", None) if test else None
        self.trace = trace

    def __enter__(self):
        """
        Context management protocol on entry to the body of the with statement.
        Just return the StringIO object.
        """
        return self

    def __exit__(self, type, value, tb):
        """
        Context management protocol on exit from the body of the with statement.
        If trace is ON, it emits the recordings into stderr.  Always add the
        recordings to our session object.  And close the StringIO object, too.
        """
        if self.trace:
            print >> sys.stderr, self.getvalue()
        if self.session:
            print >> self.session, self.getvalue()
        self.close()

# From 2.7's subprocess.check_output() convenience function.
# Return a tuple (stdoutdata, stderrdata).
def system(commands, **kwargs):
    r"""Run an os command with arguments and return its output as a byte string.

    If the exit code was non-zero it raises a CalledProcessError.  The
    CalledProcessError object will have the return code in the returncode
    attribute and output in the output attribute.

    The arguments are the same as for the Popen constructor.  Example:

    >>> check_output(["ls", "-l", "/dev/null"])
    'crw-rw-rw- 1 root root 1, 3 Oct 18  2007 /dev/null\n'

    The stdout argument is not allowed as it is used internally.
    To capture standard error in the result, use stderr=STDOUT.

    >>> check_output(["/bin/sh", "-c",
    ...               "ls -l non_existent_file ; exit 0"],
    ...              stderr=STDOUT)
    'ls: non_existent_file: No such file or directory\n'
    """

    # Assign the sender object to variable 'test' and remove it from kwargs.
    test = kwargs.pop('sender', None)

    separator = None
    separator = " && " if os.name == "nt" else "; "
    # [['make', 'clean', 'foo'], ['make', 'foo']] -> ['make clean foo', 'make foo']
    commandList = [' '.join(x) for x in commands]
    # ['make clean foo', 'make foo'] -> 'make clean foo; make foo'
    shellCommand = separator.join(commandList)

    if 'stdout' in kwargs:
        raise ValueError('stdout argument not allowed, it will be overridden.')
    if 'shell' in kwargs and kwargs['shell']==False:
        raise ValueError('shell=False not allowed')
    process = Popen(shellCommand, stdout=PIPE, stderr=PIPE, shell=True, **kwargs)
    pid = process.pid
    output, error = process.communicate()
    retcode = process.poll()

    # Enable trace on failure return while tracking down FreeBSD buildbot issues
    trace = traceAlways
    if not trace and retcode and sys.platform.startswith("freebsd"):
        trace = True

    with recording(test, trace) as sbuf:
        print >> sbuf
        print >> sbuf, "os command:", shellCommand
        print >> sbuf, "with pid:", pid
        print >> sbuf, "stdout:", output
        print >> sbuf, "stderr:", error
        print >> sbuf, "retcode:", retcode
        print >> sbuf

    if retcode:
        cmd = kwargs.get("args")
        if cmd is None:
            cmd = shellCommand
        raise CalledProcessError(retcode, cmd)
    return (output, error)

def getsource_if_available(obj):
    """
    Return the text of the source code for an object if available.  Otherwise,
    a print representation is returned.
    """
    import inspect
    try:
        return inspect.getsource(obj)
    except:
        return repr(obj)

def builder_module():
    if sys.platform.startswith("freebsd"):
        return __import__("builder_freebsd")
    return __import__("builder_" + sys.platform)

#
# Decorators for categorizing test cases.
#

from functools import wraps
def python_api_test(func):
    """Decorate the item as a Python API only test."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@python_api_test can only be used to decorate a test method")
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            if lldb.dont_do_python_api_test:
                self.skipTest("python api tests")
        except AttributeError:
            pass
        return func(self, *args, **kwargs)

    # Mark this function as such to separate them from lldb command line tests.
    wrapper.__python_api_test__ = True
    return wrapper

def lldbmi_test(func):
    """Decorate the item as a lldb-mi only test."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@lldbmi_test can only be used to decorate a test method")
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            if lldb.dont_do_lldbmi_test:
                self.skipTest("lldb-mi tests")
        except AttributeError:
            pass
        return func(self, *args, **kwargs)

    # Mark this function as such to separate them from lldb command line tests.
    wrapper.__lldbmi_test__ = True
    return wrapper

def benchmarks_test(func):
    """Decorate the item as a benchmarks test."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@benchmarks_test can only be used to decorate a test method")
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            if not lldb.just_do_benchmarks_test:
                self.skipTest("benchmarks tests")
        except AttributeError:
            pass
        return func(self, *args, **kwargs)

    # Mark this function as such to separate them from the regular tests.
    wrapper.__benchmarks_test__ = True
    return wrapper

def dsym_test(func):
    """Decorate the item as a dsym test."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@dsym_test can only be used to decorate a test method")
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            if lldb.dont_do_dsym_test:
                self.skipTest("dsym tests")
        except AttributeError:
            pass
        return func(self, *args, **kwargs)

    # Mark this function as such to separate them from the regular tests.
    wrapper.__dsym_test__ = True
    return wrapper

def dwarf_test(func):
    """Decorate the item as a dwarf test."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@dwarf_test can only be used to decorate a test method")
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            if lldb.dont_do_dwarf_test:
                self.skipTest("dwarf tests")
        except AttributeError:
            pass
        return func(self, *args, **kwargs)

    # Mark this function as such to separate them from the regular tests.
    wrapper.__dwarf_test__ = True
    return wrapper

def debugserver_test(func):
    """Decorate the item as a debugserver test."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@debugserver_test can only be used to decorate a test method")
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            if lldb.dont_do_debugserver_test:
                self.skipTest("debugserver tests")
        except AttributeError:
            pass
        return func(self, *args, **kwargs)

    # Mark this function as such to separate them from the regular tests.
    wrapper.__debugserver_test__ = True
    return wrapper

def llgs_test(func):
    """Decorate the item as a lldb-gdbserver test."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@llgs_test can only be used to decorate a test method")
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            if lldb.dont_do_llgs_test:
                self.skipTest("llgs tests")
        except AttributeError:
            pass
        return func(self, *args, **kwargs)

    # Mark this function as such to separate them from the regular tests.
    wrapper.__llgs_test__ = True
    return wrapper

def not_remote_testsuite_ready(func):
    """Decorate the item as a test which is not ready yet for remote testsuite."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@not_remote_testsuite_ready can only be used to decorate a test method")
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            if lldb.lldbtest_remote_sandbox:
                self.skipTest("not ready for remote testsuite")
        except AttributeError:
            pass
        return func(self, *args, **kwargs)

    # Mark this function as such to separate them from the regular tests.
    wrapper.__not_ready_for_remote_testsuite_test__ = True
    return wrapper

def expectedFailure(expected_fn, bugnumber=None):
    def expectedFailure_impl(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            from unittest2 import case
            self = args[0]
            try:
                func(*args, **kwargs)
            except Exception:
                if expected_fn(self):
                    raise case._ExpectedFailure(sys.exc_info(), bugnumber)
                else:
                    raise
            if expected_fn(self):
                raise case._UnexpectedSuccess(sys.exc_info(), bugnumber)
        return wrapper
    if bugnumber: 
        if callable(bugnumber):
            return expectedFailure_impl(bugnumber)
        else:
            return expectedFailure_impl

def expectedFailureCompiler(compiler, compiler_version=None, bugnumber=None):
    if compiler_version is None:
        compiler_version=['=', None]
    def fn(self):
        return compiler in self.getCompiler() and self.expectedCompilerVersion(compiler_version)
    if bugnumber: return expectedFailure(fn, bugnumber)

def expectedFailureClang(bugnumber=None):
    if bugnumber: return expectedFailureCompiler('clang', None, bugnumber)

def expectedFailureGcc(bugnumber=None, compiler_version=None):
    if bugnumber: return expectedFailureCompiler('gcc', compiler_version, bugnumber)

def expectedFailureIcc(bugnumber=None):
    if bugnumber: return expectedFailureCompiler('icc', None, bugnumber)

def expectedFailureArch(arch, bugnumber=None):
    def fn(self):
        return arch in self.getArchitecture()
    if bugnumber: return expectedFailure(fn, bugnumber)

def expectedFailurei386(bugnumber=None):
    if bugnumber: return expectedFailureArch('i386', bugnumber)

def expectedFailurex86_64(bugnumber=None):
    if bugnumber: return expectedFailureArch('x86_64', bugnumber)

def expectedFailureOS(os, bugnumber=None, compilers=None):
    def fn(self):
        return os in sys.platform and self.expectedCompiler(compilers)
    if bugnumber: return expectedFailure(fn, bugnumber)

def expectedFailureDarwin(bugnumber=None, compilers=None):
    if bugnumber: return expectedFailureOS('darwin', bugnumber, compilers)

def expectedFailureFreeBSD(bugnumber=None, compilers=None):
    if bugnumber: return expectedFailureOS('freebsd', bugnumber, compilers)

def expectedFailureLinux(bugnumber=None, compilers=None):
    if bugnumber: return expectedFailureOS('linux', bugnumber, compilers)

def skipIfRemote(func):
    """Decorate the item to skip tests if testing remotely."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfRemote can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        if lldb.remote_platform:
            self = args[0]
            self.skipTest("skip on remote platform")
        else:
            func(*args, **kwargs)
    return wrapper

def skipIfRemoteDueToDeadlock(func):
    """Decorate the item to skip tests if testing remotely due to the test deadlocking."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfRemote can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        if lldb.remote_platform:
            self = args[0]
            self.skipTest("skip on remote platform (deadlocks)")
        else:
            func(*args, **kwargs)
    return wrapper

def skipIfFreeBSD(func):
    """Decorate the item to skip tests that should be skipped on FreeBSD."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfFreeBSD can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        self = args[0]
        platform = sys.platform
        if "freebsd" in platform:
            self.skipTest("skip on FreeBSD")
        else:
            func(*args, **kwargs)
    return wrapper

def skipIfLinux(func):
    """Decorate the item to skip tests that should be skipped on Linux."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfLinux can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        self = args[0]
        platform = sys.platform
        if "linux" in platform:
            self.skipTest("skip on linux")
        else:
            func(*args, **kwargs)
    return wrapper

def skipIfNoSBHeaders(func):
    """Decorate the item to mark tests that should be skipped when LLDB is built with no SB API headers."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfNoSBHeaders can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        self = args[0]
        if sys.platform.startswith("darwin"):
            header = os.path.join(self.lib_dir, 'LLDB.framework', 'Versions','Current','Headers','LLDB.h')
        else:
            header = os.path.join(os.environ["LLDB_SRC"], "include", "lldb", "API", "LLDB.h")
        platform = sys.platform
        if not os.path.exists(header):
            self.skipTest("skip because LLDB.h header not found")
        else:
            func(*args, **kwargs)
    return wrapper

def skipIfWindows(func):
    """Decorate the item to skip tests that should be skipped on Windows."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfWindows can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        self = args[0]
        platform = sys.platform
        if "win32" in platform:
            self.skipTest("skip on Windows")
        else:
            func(*args, **kwargs)
    return wrapper

def skipIfDarwin(func):
    """Decorate the item to skip tests that should be skipped on Darwin."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfDarwin can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        self = args[0]
        platform = sys.platform
        if "darwin" in platform:
            self.skipTest("skip on darwin")
        else:
            func(*args, **kwargs)
    return wrapper


def skipIfLinuxClang(func):
    """Decorate the item to skip tests that should be skipped if building on 
       Linux with clang.
    """
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfLinuxClang can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        self = args[0]
        compiler = self.getCompiler()
        platform = sys.platform
        if "clang" in compiler and "linux" in platform:
            self.skipTest("skipping because Clang is used on Linux")
        else:
            func(*args, **kwargs)
    return wrapper

def skipIfGcc(func):
    """Decorate the item to skip tests that should be skipped if building with gcc ."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfGcc can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        self = args[0]
        compiler = self.getCompiler()
        if "gcc" in compiler:
            self.skipTest("skipping because gcc is the test compiler")
        else:
            func(*args, **kwargs)
    return wrapper

def skipIfIcc(func):
    """Decorate the item to skip tests that should be skipped if building with icc ."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfIcc can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        self = args[0]
        compiler = self.getCompiler()
        if "icc" in compiler:
            self.skipTest("skipping because icc is the test compiler")
        else:
            func(*args, **kwargs)
    return wrapper

def skipIfi386(func):
    """Decorate the item to skip tests that should be skipped if building 32-bit."""
    if isinstance(func, type) and issubclass(func, unittest2.TestCase):
        raise Exception("@skipIfi386 can only be used to decorate a test method")
    @wraps(func)
    def wrapper(*args, **kwargs):
        from unittest2 import case
        self = args[0]
        if "i386" == self.getArchitecture():
            self.skipTest("skipping because i386 is not a supported architecture")
        else:
            func(*args, **kwargs)
    return wrapper


class _PlatformContext(object):
    """Value object class which contains platform-specific options."""

    def __init__(self, shlib_environment_var, shlib_prefix, shlib_extension):
        self.shlib_environment_var = shlib_environment_var
        self.shlib_prefix = shlib_prefix
        self.shlib_extension = shlib_extension


class Base(unittest2.TestCase):
    """
    Abstract base for performing lldb (see TestBase) or other generic tests (see
    BenchBase for one example).  lldbtest.Base works with the test driver to
    accomplish things.
    
    """

    # The concrete subclass should override this attribute.
    mydir = None

    # Keep track of the old current working directory.
    oldcwd = None

    @staticmethod
    def compute_mydir(test_file):
        '''Subclasses should call this function to correctly calculate the required "mydir" attribute as follows: 
            
            mydir = TestBase.compute_mydir(__file__)'''
        test_dir = os.path.dirname(test_file)
        return test_dir[len(os.environ["LLDB_TEST"])+1:]
    
    def TraceOn(self):
        """Returns True if we are in trace mode (tracing detailed test execution)."""
        return traceAlways
    
    @classmethod
    def setUpClass(cls):
        """
        Python unittest framework class setup fixture.
        Do current directory manipulation.
        """

        # Fail fast if 'mydir' attribute is not overridden.
        if not cls.mydir or len(cls.mydir) == 0:
            raise Exception("Subclasses must override the 'mydir' attribute.")

        # Save old working directory.
        cls.oldcwd = os.getcwd()

        # Change current working directory if ${LLDB_TEST} is defined.
        # See also dotest.py which sets up ${LLDB_TEST}.
        if ("LLDB_TEST" in os.environ):
            if traceAlways:
                print >> sys.stderr, "Change dir to:", os.path.join(os.environ["LLDB_TEST"], cls.mydir)
            os.chdir(os.path.join(os.environ["LLDB_TEST"], cls.mydir))

        # Set platform context.
        if sys.platform.startswith('darwin'):
            cls.platformContext = _PlatformContext('DYLD_LIBRARY_PATH', 'lib', 'dylib')
        elif sys.platform.startswith('linux') or sys.platform.startswith('freebsd'):
            cls.platformContext = _PlatformContext('LD_LIBRARY_PATH', 'lib', 'so')
        else:
            cls.platformContext = None

    @classmethod
    def tearDownClass(cls):
        """
        Python unittest framework class teardown fixture.
        Do class-wide cleanup.
        """

        if doCleanup and not lldb.skip_build_and_cleanup:
            # First, let's do the platform-specific cleanup.
            module = builder_module()
            if not module.cleanup():
                raise Exception("Don't know how to do cleanup")

            # Subclass might have specific cleanup function defined.
            if getattr(cls, "classCleanup", None):
                if traceAlways:
                    print >> sys.stderr, "Call class-specific cleanup function for class:", cls
                try:
                    cls.classCleanup()
                except:
                    exc_type, exc_value, exc_tb = sys.exc_info()
                    traceback.print_exception(exc_type, exc_value, exc_tb)

        # Restore old working directory.
        if traceAlways:
            print >> sys.stderr, "Restore dir to:", cls.oldcwd
        os.chdir(cls.oldcwd)

    @classmethod
    def skipLongRunningTest(cls):
        """
        By default, we skip long running test case.
        This can be overridden by passing '-l' to the test driver (dotest.py).
        """
        if "LLDB_SKIP_LONG_RUNNING_TEST" in os.environ and "NO" == os.environ["LLDB_SKIP_LONG_RUNNING_TEST"]:
            return False
        else:
            return True

    def setUp(self):
        """Fixture for unittest test case setup.

        It works with the test driver to conditionally skip tests and does other
        initializations."""
        #import traceback
        #traceback.print_stack()

        if "LIBCXX_PATH" in os.environ:
            self.libcxxPath = os.environ["LIBCXX_PATH"]
        else:
            self.libcxxPath = None

        if "LLDB_EXEC" in os.environ:
            self.lldbExec = os.environ["LLDB_EXEC"]
        else:
            self.lldbExec = None
        if "LLDBMI_EXEC" in os.environ:
            self.lldbMiExec = os.environ["LLDBMI_EXEC"]
        else:
            self.lldbMiExec = None
            self.dont_do_lldbmi_test = True
        if "LLDB_HERE" in os.environ:
            self.lldbHere = os.environ["LLDB_HERE"]
        else:
            self.lldbHere = None
        # If we spawn an lldb process for test (via pexpect), do not load the
        # init file unless told otherwise.
        if "NO_LLDBINIT" in os.environ and "NO" == os.environ["NO_LLDBINIT"]:
            self.lldbOption = ""
        else:
            self.lldbOption = "--no-lldbinit"

        # Assign the test method name to self.testMethodName.
        #
        # For an example of the use of this attribute, look at test/types dir.
        # There are a bunch of test cases under test/types and we don't want the
        # module cacheing subsystem to be confused with executable name "a.out"
        # used for all the test cases.
        self.testMethodName = self._testMethodName

        # Python API only test is decorated with @python_api_test,
        # which also sets the "__python_api_test__" attribute of the
        # function object to True.
        try:
            if lldb.just_do_python_api_test:
                testMethod = getattr(self, self._testMethodName)
                if getattr(testMethod, "__python_api_test__", False):
                    pass
                else:
                    self.skipTest("non python api test")
        except AttributeError:
            pass

        # lldb-mi only test is decorated with @lldbmi_test,
        # which also sets the "__lldbmi_test__" attribute of the
        # function object to True.
        try:
            if lldb.just_do_lldbmi_test:
                testMethod = getattr(self, self._testMethodName)
                if getattr(testMethod, "__lldbmi_test__", False):
                    pass
                else:
                    self.skipTest("non lldb-mi test")
        except AttributeError:
            pass

        # Benchmarks test is decorated with @benchmarks_test,
        # which also sets the "__benchmarks_test__" attribute of the
        # function object to True.
        try:
            if lldb.just_do_benchmarks_test:
                testMethod = getattr(self, self._testMethodName)
                if getattr(testMethod, "__benchmarks_test__", False):
                    pass
                else:
                    self.skipTest("non benchmarks test")
        except AttributeError:
            pass

        # This is for the case of directly spawning 'lldb'/'gdb' and interacting
        # with it using pexpect.
        self.child = None
        self.child_prompt = "(lldb) "
        # If the child is interacting with the embedded script interpreter,
        # there are two exits required during tear down, first to quit the
        # embedded script interpreter and second to quit the lldb command
        # interpreter.
        self.child_in_script_interpreter = False

        # These are for customized teardown cleanup.
        self.dict = None
        self.doTearDownCleanup = False
        # And in rare cases where there are multiple teardown cleanups.
        self.dicts = []
        self.doTearDownCleanups = False

        # List of spawned subproces.Popen objects
        self.subprocesses = []

        # List of forked process PIDs
        self.forkedProcessPids = []

        # Create a string buffer to record the session info, to be dumped into a
        # test case specific file if test failure is encountered.
        self.session = StringIO.StringIO()

        # Optimistically set __errored__, __failed__, __expected__ to False
        # initially.  If the test errored/failed, the session info
        # (self.session) is then dumped into a session specific file for
        # diagnosis.
        self.__errored__    = False
        self.__failed__     = False
        self.__expected__   = False
        # We are also interested in unexpected success.
        self.__unexpected__ = False
        # And skipped tests.
        self.__skipped__ = False

        # See addTearDownHook(self, hook) which allows the client to add a hook
        # function to be run during tearDown() time.
        self.hooks = []

        # See HideStdout(self).
        self.sys_stdout_hidden = False

        if self.platformContext:
            # set environment variable names for finding shared libraries
            self.dylibPath = self.platformContext.shlib_environment_var

    def runHooks(self, child=None, child_prompt=None, use_cmd_api=False):
        """Perform the run hooks to bring lldb debugger to the desired state.

        By default, expect a pexpect spawned child and child prompt to be
        supplied (use_cmd_api=False).  If use_cmd_api is true, ignore the child
        and child prompt and use self.runCmd() to run the hooks one by one.

        Note that child is a process spawned by pexpect.spawn().  If not, your
        test case is mostly likely going to fail.

        See also dotest.py where lldb.runHooks are processed/populated.
        """
        if not lldb.runHooks:
            self.skipTest("No runhooks specified for lldb, skip the test")
        if use_cmd_api:
            for hook in lldb.runhooks:
                self.runCmd(hook)
        else:
            if not child or not child_prompt:
                self.fail("Both child and child_prompt need to be defined.")
            for hook in lldb.runHooks:
                child.sendline(hook)
                child.expect_exact(child_prompt)

    def setAsync(self, value):
        """ Sets async mode to True/False and ensures it is reset after the testcase completes."""
        old_async = self.dbg.GetAsync()
        self.dbg.SetAsync(value)
        self.addTearDownHook(lambda: self.dbg.SetAsync(old_async))

    def cleanupSubprocesses(self):
        # Ensure any subprocesses are cleaned up
        for p in self.subprocesses:
            if p.poll() == None:
                p.terminate()
            del p
        del self.subprocesses[:]
        # Ensure any forked processes are cleaned up
        for pid in self.forkedProcessPids:
            if os.path.exists("/proc/" + str(pid)):
                os.kill(pid, signal.SIGTERM)

    def spawnSubprocess(self, executable, args=[]):
        """ Creates a subprocess.Popen object with the specified executable and arguments,
            saves it in self.subprocesses, and returns the object.
            NOTE: if using this function, ensure you also call:

              self.addTearDownHook(self.cleanupSubprocesses)

            otherwise the test suite will leak processes.
        """

        # Don't display the stdout if not in TraceOn() mode.
        proc = Popen([executable] + args,
                     stdout = open(os.devnull) if not self.TraceOn() else None,
                     stdin = PIPE)
        self.subprocesses.append(proc)
        return proc

    def forkSubprocess(self, executable, args=[]):
        """ Fork a subprocess with its own group ID.
            NOTE: if using this function, ensure you also call:

              self.addTearDownHook(self.cleanupSubprocesses)

            otherwise the test suite will leak processes.
        """
        child_pid = os.fork()
        if child_pid == 0:
            # If more I/O support is required, this can be beefed up.
            fd = os.open(os.devnull, os.O_RDWR)
            os.dup2(fd, 1)
            os.dup2(fd, 2)
            # This call causes the child to have its of group ID
            os.setpgid(0,0)
            os.execvp(executable, [executable] + args)
        # Give the child time to get through the execvp() call
        time.sleep(0.1)
        self.forkedProcessPids.append(child_pid)
        return child_pid

    def HideStdout(self):
        """Hide output to stdout from the user.

        During test execution, there might be cases where we don't want to show the
        standard output to the user.  For example,

            self.runCmd(r'''sc print "\n\n\tHello!\n"''')

        tests whether command abbreviation for 'script' works or not.  There is no
        need to show the 'Hello' output to the user as long as the 'script' command
        succeeds and we are not in TraceOn() mode (see the '-t' option).

        In this case, the test method calls self.HideStdout(self) to redirect the
        sys.stdout to a null device, and restores the sys.stdout upon teardown.

        Note that you should only call this method at most once during a test case
        execution.  Any subsequent call has no effect at all."""
        if self.sys_stdout_hidden:
            return

        self.sys_stdout_hidden = True
        old_stdout = sys.stdout
        sys.stdout = open(os.devnull, 'w')
        def restore_stdout():
            sys.stdout = old_stdout
        self.addTearDownHook(restore_stdout)

    # =======================================================================
    # Methods for customized teardown cleanups as well as execution of hooks.
    # =======================================================================

    def setTearDownCleanup(self, dictionary=None):
        """Register a cleanup action at tearDown() time with a dictinary"""
        self.dict = dictionary
        self.doTearDownCleanup = True

    def addTearDownCleanup(self, dictionary):
        """Add a cleanup action at tearDown() time with a dictinary"""
        self.dicts.append(dictionary)
        self.doTearDownCleanups = True

    def addTearDownHook(self, hook):
        """
        Add a function to be run during tearDown() time.

        Hooks are executed in a first come first serve manner.
        """
        if callable(hook):
            with recording(self, traceAlways) as sbuf:
                print >> sbuf, "Adding tearDown hook:", getsource_if_available(hook)
            self.hooks.append(hook)
        
        return self

    def deletePexpectChild(self):
        # This is for the case of directly spawning 'lldb' and interacting with it
        # using pexpect.
        if self.child and self.child.isalive():
            import pexpect
            with recording(self, traceAlways) as sbuf:
                print >> sbuf, "tearing down the child process...."
            try:
                if self.child_in_script_interpreter:
                    self.child.sendline('quit()')
                    self.child.expect_exact(self.child_prompt)
                self.child.sendline('settings set interpreter.prompt-on-quit false')
                self.child.sendline('quit')
                self.child.expect(pexpect.EOF)
            except (ValueError, pexpect.ExceptionPexpect):
                # child is already terminated
                pass
            finally:
                # Give it one final blow to make sure the child is terminated.
                self.child.close()

    def tearDown(self):
        """Fixture for unittest test case teardown."""
        #import traceback
        #traceback.print_stack()

        self.deletePexpectChild()

        # Check and run any hook functions.
        for hook in reversed(self.hooks):
            with recording(self, traceAlways) as sbuf:
                print >> sbuf, "Executing tearDown hook:", getsource_if_available(hook)
            import inspect
            hook_argc = len(inspect.getargspec(hook).args)
            if hook_argc == 0 or getattr(hook,'im_self',None):
                hook()
            elif hook_argc == 1:
                hook(self)
            else:
                hook() # try the plain call and hope it works

        del self.hooks

        # Perform registered teardown cleanup.
        if doCleanup and self.doTearDownCleanup:
            self.cleanup(dictionary=self.dict)

        # In rare cases where there are multiple teardown cleanups added.
        if doCleanup and self.doTearDownCleanups:
            if self.dicts:
                for dict in reversed(self.dicts):
                    self.cleanup(dictionary=dict)

        # Decide whether to dump the session info.
        self.dumpSessionInfo()

    # =========================================================
    # Various callbacks to allow introspection of test progress
    # =========================================================

    def markError(self):
        """Callback invoked when an error (unexpected exception) errored."""
        self.__errored__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "ERROR" to the stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            print >> sbuf, "ERROR"

    def markFailure(self):
        """Callback invoked when a failure (test assertion failure) occurred."""
        self.__failed__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "FAIL" to the stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            print >> sbuf, "FAIL"

    def markExpectedFailure(self,err,bugnumber):
        """Callback invoked when an expected failure/error occurred."""
        self.__expected__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "expected failure" to the
            # stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            if bugnumber == None:
                print >> sbuf, "expected failure"
            else:
                print >> sbuf, "expected failure (problem id:" + str(bugnumber) + ")"	

    def markSkippedTest(self):
        """Callback invoked when a test is skipped."""
        self.__skipped__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "skipped test" to the
            # stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            print >> sbuf, "skipped test"

    def markUnexpectedSuccess(self, bugnumber):
        """Callback invoked when an unexpected success occurred."""
        self.__unexpected__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "unexpected success" to the
            # stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            if bugnumber == None:
                print >> sbuf, "unexpected success"
            else:
                print >> sbuf, "unexpected success (problem id:" + str(bugnumber) + ")"	

    def dumpSessionInfo(self):
        """
        Dump the debugger interactions leading to a test error/failure.  This
        allows for more convenient postmortem analysis.

        See also LLDBTestResult (dotest.py) which is a singlton class derived
        from TextTestResult and overwrites addError, addFailure, and
        addExpectedFailure methods to allow us to to mark the test instance as
        such.
        """

        # We are here because self.tearDown() detected that this test instance
        # either errored or failed.  The lldb.test_result singleton contains
        # two lists (erros and failures) which get populated by the unittest
        # framework.  Look over there for stack trace information.
        #
        # The lists contain 2-tuples of TestCase instances and strings holding
        # formatted tracebacks.
        #
        # See http://docs.python.org/library/unittest.html#unittest.TestResult.
        if self.__errored__:
            pairs = lldb.test_result.errors
            prefix = 'Error'
        elif self.__failed__:
            pairs = lldb.test_result.failures
            prefix = 'Failure'
        elif self.__expected__:
            pairs = lldb.test_result.expectedFailures
            prefix = 'ExpectedFailure'
        elif self.__skipped__:
            prefix = 'SkippedTest'
        elif self.__unexpected__:
            prefix = "UnexpectedSuccess"
        else:
            # Simply return, there's no session info to dump!
            return

        if not self.__unexpected__ and not self.__skipped__:
            for test, traceback in pairs:
                if test is self:
                    print >> self.session, traceback

        testMethod = getattr(self, self._testMethodName)
        if getattr(testMethod, "__benchmarks_test__", False):
            benchmarks = True
        else:
            benchmarks = False

        dname = os.path.join(os.environ["LLDB_TEST"],
                             os.environ["LLDB_SESSION_DIRNAME"])
        if not os.path.isdir(dname):
            os.mkdir(dname)
        compiler = self.getCompiler()
        if compiler[1] == ':':
            compiler = compiler[2:]

        fname = os.path.join(dname, "%s-%s-%s-%s.log" % (prefix, self.getArchitecture(), "_".join(compiler.split(os.path.sep)), self.id()))
        with open(fname, "w") as f:
            import datetime
            print >> f, "Session info generated @", datetime.datetime.now().ctime()
            print >> f, self.session.getvalue()
            print >> f, "To rerun this test, issue the following command from the 'test' directory:\n"
            print >> f, "./dotest.py %s -v %s -f %s.%s" % (self.getRunOptions(),
                                                           ('+b' if benchmarks else '-t'),
                                                           self.__class__.__name__,
                                                           self._testMethodName)

    # ====================================================
    # Config. methods supported through a plugin interface
    # (enables reading of the current test configuration)
    # ====================================================

    def getArchitecture(self):
        """Returns the architecture in effect the test suite is running with."""
        module = builder_module()
        return module.getArchitecture()

    def getCompiler(self):
        """Returns the compiler in effect the test suite is running with."""
        module = builder_module()
        return module.getCompiler()

    def getCompilerBinary(self):
        """Returns the compiler binary the test suite is running with."""
        return self.getCompiler().split()[0]

    def getCompilerVersion(self):
        """ Returns a string that represents the compiler version.
            Supports: llvm, clang.
        """
        from lldbutil import which
        version = 'unknown'

        compiler = self.getCompilerBinary()
        version_output = system([[which(compiler), "-v"]])[1]
        for line in version_output.split(os.linesep):
            m = re.search('version ([0-9\.]+)', line)
            if m:
                version = m.group(1)
        return version

    def isIntelCompiler(self):
        """ Returns true if using an Intel (ICC) compiler, false otherwise. """
        return any([x in self.getCompiler() for x in ["icc", "icpc", "icl"]])

    def expectedCompilerVersion(self, compiler_version):
        """Returns True iff compiler_version[1] matches the current compiler version.
           Use compiler_version[0] to specify the operator used to determine if a match has occurred.
           Any operator other than the following defaults to an equality test:
             '>', '>=', "=>", '<', '<=', '=<', '!=', "!" or 'not'
        """
        if (compiler_version == None):
            return True
        operator = str(compiler_version[0])
        version = compiler_version[1]

        if (version == None):
            return True
        if (operator == '>'):
            return self.getCompilerVersion() > version
        if (operator == '>=' or operator == '=>'): 
            return self.getCompilerVersion() >= version
        if (operator == '<'):
            return self.getCompilerVersion() < version
        if (operator == '<=' or operator == '=<'):
            return self.getCompilerVersion() <= version
        if (operator == '!=' or operator == '!' or operator == 'not'):
            return str(version) not in str(self.getCompilerVersion())
        return str(version) in str(self.getCompilerVersion())

    def expectedCompiler(self, compilers):
        """Returns True iff any element of compilers is a sub-string of the current compiler."""
        if (compilers == None):
            return True

        for compiler in compilers:
            if compiler in self.getCompiler():
                return True

        return False

    def getRunOptions(self):
        """Command line option for -A and -C to run this test again, called from
        self.dumpSessionInfo()."""
        arch = self.getArchitecture()
        comp = self.getCompiler()
        if arch:
            option_str = "-A " + arch
        else:
            option_str = ""
        if comp:
            option_str += " -C " + comp
        return option_str

    # ==================================================
    # Build methods supported through a plugin interface
    # ==================================================

    def getstdlibFlag(self):
        """ Returns the proper -stdlib flag, or empty if not required."""
        if sys.platform.startswith("darwin") or sys.platform.startswith("freebsd"):
            stdlibflag = "-stdlib=libc++"
        else:
            stdlibflag = ""
        return stdlibflag

    def getstdFlag(self):
        """ Returns the proper stdflag. """
        if "gcc" in self.getCompiler() and "4.6" in self.getCompilerVersion():
          stdflag = "-std=c++0x"
        else:
          stdflag = "-std=c++11"
        return stdflag

    def buildDriver(self, sources, exe_name):
        """ Platform-specific way to build a program that links with LLDB (via the liblldb.so
            or LLDB.framework).
        """

        stdflag = self.getstdFlag()
        stdlibflag = self.getstdlibFlag()

        if sys.platform.startswith("darwin"):
            dsym = os.path.join(self.lib_dir, 'LLDB.framework', 'LLDB')
            d = {'CXX_SOURCES' : sources,
                 'EXE' : exe_name,
                 'CFLAGS_EXTRAS' : "%s %s" % (stdflag, stdlibflag),
                 'FRAMEWORK_INCLUDES' : "-F%s" % self.lib_dir,
                 'LD_EXTRAS' : "%s -Wl,-rpath,%s" % (dsym, self.lib_dir),
                }
        elif sys.platform.startswith('freebsd') or sys.platform.startswith("linux") or os.environ.get('LLDB_BUILD_TYPE') == 'Makefile':
            d = {'CXX_SOURCES' : sources, 
                 'EXE' : exe_name,
                 'CFLAGS_EXTRAS' : "%s %s -I%s" % (stdflag, stdlibflag, os.path.join(os.environ["LLDB_SRC"], "include")),
                 'LD_EXTRAS' : "-L%s -llldb" % self.lib_dir}
        if self.TraceOn():
            print "Building LLDB Driver (%s) from sources %s" % (exe_name, sources)

        self.buildDefault(dictionary=d)

    def buildLibrary(self, sources, lib_name):
        """Platform specific way to build a default library. """

        stdflag = self.getstdFlag()

        if sys.platform.startswith("darwin"):
            dsym = os.path.join(self.lib_dir, 'LLDB.framework', 'LLDB')
            d = {'DYLIB_CXX_SOURCES' : sources,
                 'DYLIB_NAME' : lib_name,
                 'CFLAGS_EXTRAS' : "%s -stdlib=libc++" % stdflag,
                 'FRAMEWORK_INCLUDES' : "-F%s" % self.lib_dir,
                 'LD_EXTRAS' : "%s -Wl,-rpath,%s -dynamiclib" % (dsym, self.lib_dir),
                }
        elif sys.platform.startswith('freebsd') or sys.platform.startswith("linux") or os.environ.get('LLDB_BUILD_TYPE') == 'Makefile':
            d = {'DYLIB_CXX_SOURCES' : sources,
                 'DYLIB_NAME' : lib_name,
                 'CFLAGS_EXTRAS' : "%s -I%s -fPIC" % (stdflag, os.path.join(os.environ["LLDB_SRC"], "include")),
                 'LD_EXTRAS' : "-shared -L%s -llldb" % self.lib_dir}
        if self.TraceOn():
            print "Building LLDB Library (%s) from sources %s" % (lib_name, sources)

        self.buildDefault(dictionary=d)

    def buildProgram(self, sources, exe_name):
        """ Platform specific way to build an executable from C/C++ sources. """
        d = {'CXX_SOURCES' : sources,
             'EXE' : exe_name}
        self.buildDefault(dictionary=d)

    def buildDefault(self, architecture=None, compiler=None, dictionary=None, clean=True):
        """Platform specific way to build the default binaries."""
        if lldb.skip_build_and_cleanup:
            return
        module = builder_module()
        if not module.buildDefault(self, architecture, compiler, dictionary, clean):
            raise Exception("Don't know how to build default binary")

    def buildDsym(self, architecture=None, compiler=None, dictionary=None, clean=True):
        """Platform specific way to build binaries with dsym info."""
        if lldb.skip_build_and_cleanup:
            return
        module = builder_module()
        if not module.buildDsym(self, architecture, compiler, dictionary, clean):
            raise Exception("Don't know how to build binary with dsym")

    def buildDwarf(self, architecture=None, compiler=None, dictionary=None, clean=True):
        """Platform specific way to build binaries with dwarf maps."""
        if lldb.skip_build_and_cleanup:
            return
        module = builder_module()
        if not module.buildDwarf(self, architecture, compiler, dictionary, clean):
            raise Exception("Don't know how to build binary with dwarf")

    def findBuiltClang(self):
        """Tries to find and use Clang from the build directory as the compiler (instead of the system compiler)."""
        paths_to_try = [
          "llvm-build/Release+Asserts/x86_64/Release+Asserts/bin/clang",
          "llvm-build/Debug+Asserts/x86_64/Debug+Asserts/bin/clang",
          "llvm-build/Release/x86_64/Release/bin/clang",
          "llvm-build/Debug/x86_64/Debug/bin/clang",
        ]
        lldb_root_path = os.path.join(os.path.dirname(__file__), "..")
        for p in paths_to_try:
            path = os.path.join(lldb_root_path, p)
            if os.path.exists(path):
                return path
        
        return os.environ["CC"]

    def getBuildFlags(self, use_cpp11=True, use_libcxx=False, use_libstdcxx=False, use_pthreads=True):
        """ Returns a dictionary (which can be provided to build* functions above) which
            contains OS-specific build flags.
        """
        cflags = ""

        # On Mac OS X, unless specifically requested to use libstdc++, use libc++
        if not use_libstdcxx and sys.platform.startswith('darwin'):
            use_libcxx = True

        if use_libcxx and self.libcxxPath:
            cflags += "-stdlib=libc++ "
            if self.libcxxPath:
                libcxxInclude = os.path.join(self.libcxxPath, "include")
                libcxxLib = os.path.join(self.libcxxPath, "lib")
                if os.path.isdir(libcxxInclude) and os.path.isdir(libcxxLib):
                    cflags += "-nostdinc++ -I%s -L%s -Wl,-rpath,%s " % (libcxxInclude, libcxxLib, libcxxLib)

        if use_cpp11:
            cflags += "-std="
            if "gcc" in self.getCompiler() and "4.6" in self.getCompilerVersion():
                cflags += "c++0x"
            else:
                cflags += "c++11"
        if sys.platform.startswith("darwin") or sys.platform.startswith("freebsd"):
            cflags += " -stdlib=libc++"
        elif "clang" in self.getCompiler():
            cflags += " -stdlib=libstdc++"

        if use_pthreads:
            ldflags = "-lpthread"

        return {'CFLAGS_EXTRAS' : cflags,
                'LD_EXTRAS' : ldflags,
               }

    def cleanup(self, dictionary=None):
        """Platform specific way to do cleanup after build."""
        if lldb.skip_build_and_cleanup:
            return
        module = builder_module()
        if not module.cleanup(self, dictionary):
            raise Exception("Don't know how to do cleanup with dictionary: "+dictionary)

    def getLLDBLibraryEnvVal(self):
        """ Returns the path that the OS-specific library search environment variable
            (self.dylibPath) should be set to in order for a program to find the LLDB
            library. If an environment variable named self.dylibPath is already set,
            the new path is appended to it and returned.
        """
        existing_library_path = os.environ[self.dylibPath] if self.dylibPath in os.environ else None
        if existing_library_path:
            return "%s:%s" % (existing_library_path, self.lib_dir)
        elif sys.platform.startswith("darwin"):
            return os.path.join(self.lib_dir, 'LLDB.framework')
        else:
            return self.lib_dir

    def getLibcPlusPlusLibs(self):
        if sys.platform.startswith('freebsd'):
            return ['libc++.so.1']
        else:
            return ['libc++.1.dylib','libc++abi.dylib']

class TestBase(Base):
    """
    This abstract base class is meant to be subclassed.  It provides default
    implementations for setUpClass(), tearDownClass(), setUp(), and tearDown(),
    among other things.

    Important things for test class writers:

        - Overwrite the mydir class attribute, otherwise your test class won't
          run.  It specifies the relative directory to the top level 'test' so
          the test harness can change to the correct working directory before
          running your test.

        - The setUp method sets up things to facilitate subsequent interactions
          with the debugger as part of the test.  These include:
              - populate the test method name
              - create/get a debugger set with synchronous mode (self.dbg)
              - get the command interpreter from with the debugger (self.ci)
              - create a result object for use with the command interpreter
                (self.res)
              - plus other stuffs

        - The tearDown method tries to perform some necessary cleanup on behalf
          of the test to return the debugger to a good state for the next test.
          These include:
              - execute any tearDown hooks registered by the test method with
                TestBase.addTearDownHook(); examples can be found in
                settings/TestSettings.py
              - kill the inferior process associated with each target, if any,
                and, then delete the target from the debugger's target list
              - perform build cleanup before running the next test method in the
                same test class; examples of registering for this service can be
                found in types/TestIntegerTypes.py with the call:
                    - self.setTearDownCleanup(dictionary=d)

        - Similarly setUpClass and tearDownClass perform classwise setup and
          teardown fixtures.  The tearDownClass method invokes a default build
          cleanup for the entire test class;  also, subclasses can implement the
          classmethod classCleanup(cls) to perform special class cleanup action.

        - The instance methods runCmd and expect are used heavily by existing
          test cases to send a command to the command interpreter and to perform
          string/pattern matching on the output of such command execution.  The
          expect method also provides a mode to peform string/pattern matching
          without running a command.

        - The build methods buildDefault, buildDsym, and buildDwarf are used to
          build the binaries used during a particular test scenario.  A plugin
          should be provided for the sys.platform running the test suite.  The
          Mac OS X implementation is located in plugins/darwin.py.
    """

    # Maximum allowed attempts when launching the inferior process.
    # Can be overridden by the LLDB_MAX_LAUNCH_COUNT environment variable.
    maxLaunchCount = 3;

    # Time to wait before the next launching attempt in second(s).
    # Can be overridden by the LLDB_TIME_WAIT_NEXT_LAUNCH environment variable.
    timeWaitNextLaunch = 1.0;

    def doDelay(self):
        """See option -w of dotest.py."""
        if ("LLDB_WAIT_BETWEEN_TEST_CASES" in os.environ and
            os.environ["LLDB_WAIT_BETWEEN_TEST_CASES"] == 'YES'):
            waitTime = 1.0
            if "LLDB_TIME_WAIT_BETWEEN_TEST_CASES" in os.environ:
                waitTime = float(os.environ["LLDB_TIME_WAIT_BETWEEN_TEST_CASES"])
            time.sleep(waitTime)

    # Returns the list of categories to which this test case belongs
    # by default, look for a ".categories" file, and read its contents
    # if no such file exists, traverse the hierarchy - we guarantee
    # a .categories to exist at the top level directory so we do not end up
    # looping endlessly - subclasses are free to define their own categories
    # in whatever way makes sense to them
    def getCategories(self):
        import inspect
        import os.path
        folder = inspect.getfile(self.__class__)
        folder = os.path.dirname(folder)
        while folder != '/':
                categories_file_name = os.path.join(folder,".categories")
                if os.path.exists(categories_file_name):
                        categories_file = open(categories_file_name,'r')
                        categories = categories_file.readline()
                        categories_file.close()
                        categories = str.replace(categories,'\n','')
                        categories = str.replace(categories,'\r','')
                        return categories.split(',')
                else:
                        folder = os.path.dirname(folder)
                        continue

    def setUp(self):
        #import traceback
        #traceback.print_stack()

        # Works with the test driver to conditionally skip tests via decorators.
        Base.setUp(self)

        try:
            if lldb.blacklist:
                className = self.__class__.__name__
                classAndMethodName = "%s.%s" % (className, self._testMethodName)
                if className in lldb.blacklist:
                    self.skipTest(lldb.blacklist.get(className))
                elif classAndMethodName in lldb.blacklist:
                    self.skipTest(lldb.blacklist.get(classAndMethodName))
        except AttributeError:
            pass

        # Insert some delay between successive test cases if specified.
        self.doDelay()

        if "LLDB_MAX_LAUNCH_COUNT" in os.environ:
            self.maxLaunchCount = int(os.environ["LLDB_MAX_LAUNCH_COUNT"])

        if "LLDB_TIME_WAIT_NEXT_LAUNCH" in os.environ:
            self.timeWaitNextLaunch = float(os.environ["LLDB_TIME_WAIT_NEXT_LAUNCH"])

        # Create the debugger instance if necessary.
        try:
            self.dbg = lldb.DBG
        except AttributeError:
            self.dbg = lldb.SBDebugger.Create()

        if not self.dbg:
            raise Exception('Invalid debugger instance')

        #
        # Warning: MAJOR HACK AHEAD!
        # If we are running testsuite remotely (by checking lldb.lldbtest_remote_sandbox),
        # redefine the self.dbg.CreateTarget(filename) method to execute a "file filename"
        # command, instead.  See also runCmd() where it decorates the "file filename" call
        # with additional functionality when running testsuite remotely.
        #
        if lldb.lldbtest_remote_sandbox:
            def DecoratedCreateTarget(arg):
                self.runCmd("file %s" % arg)
                target = self.dbg.GetSelectedTarget()
                #
                # SBtarget.LaunchSimple () currently not working for remote platform?
                # johnny @ 04/23/2012
                #
                def DecoratedLaunchSimple(argv, envp, wd):
                    self.runCmd("run")
                    return target.GetProcess()
                target.LaunchSimple = DecoratedLaunchSimple

                return target
            self.dbg.CreateTarget = DecoratedCreateTarget
            if self.TraceOn():
                print "self.dbg.Create is redefined to:\n%s" % getsource_if_available(DecoratedCreateTarget)

        # We want our debugger to be synchronous.
        self.dbg.SetAsync(False)

        # Retrieve the associated command interpreter instance.
        self.ci = self.dbg.GetCommandInterpreter()
        if not self.ci:
            raise Exception('Could not get the command interpreter')

        # And the result object.
        self.res = lldb.SBCommandReturnObject()

        # Run global pre-flight code, if defined via the config file.
        if lldb.pre_flight:
            lldb.pre_flight(self)

        if lldb.remote_platform:
            #remote_test_dir = os.path.join(lldb.remote_platform_working_dir, self.mydir)
            remote_test_dir = os.path.join(lldb.remote_platform_working_dir, 
                                           self.getArchitecture(), 
                                           str(self.test_number), 
                                           self.mydir)
            error = lldb.remote_platform.MakeDirectory(remote_test_dir, 0700)
            if error.Success():
                lldb.remote_platform.SetWorkingDirectory(remote_test_dir)
            else:
                print "error: making remote directory '%s': %s" % (remote_test_dir, error)
    
    def registerSharedLibrariesWithTarget(self, target, shlibs):
        '''If we are remotely running the test suite, register the shared libraries with the target so they get uploaded, otherwise do nothing
        
        Any modules in the target that have their remote install file specification set will
        get uploaded to the remote host. This function registers the local copies of the
        shared libraries with the target and sets their remote install locations so they will
        be uploaded when the target is run.
        '''
        if not shlibs or not self.platformContext:
            return None

        shlib_environment_var = self.platformContext.shlib_environment_var
        shlib_prefix = self.platformContext.shlib_prefix
        shlib_extension = '.' + self.platformContext.shlib_extension

        working_dir = self.get_process_working_directory()
        environment = ['%s=%s' % (shlib_environment_var, working_dir)]
        # Add any shared libraries to our target if remote so they get
        # uploaded into the working directory on the remote side
        for name in shlibs:
            # The path can be a full path to a shared library, or a make file name like "Foo" for
            # "libFoo.dylib" or "libFoo.so", or "Foo.so" for "Foo.so" or "libFoo.so", or just a
            # basename like "libFoo.so". So figure out which one it is and resolve the local copy
            # of the shared library accordingly
            if os.path.exists(name):
                local_shlib_path = name # name is the full path to the local shared library
            else:
                # Check relative names
                local_shlib_path = os.path.join(os.getcwd(), shlib_prefix + name + shlib_extension)
                if not os.path.exists(local_shlib_path):
                    local_shlib_path = os.path.join(os.getcwd(), name + shlib_extension)
                    if not os.path.exists(local_shlib_path):
                        local_shlib_path = os.path.join(os.getcwd(), name)

                # Make sure we found the local shared library in the above code
                self.assertTrue(os.path.exists(local_shlib_path))

            # Add the shared library to our target
            shlib_module = target.AddModule(local_shlib_path, None, None, None)
            if lldb.remote_platform:
                # We must set the remote install location if we want the shared library
                # to get uploaded to the remote target
                remote_shlib_path = os.path.join(lldb.remote_platform.GetWorkingDirectory(), os.path.basename(local_shlib_path))
                shlib_module.SetRemoteInstallFileSpec(lldb.SBFileSpec(remote_shlib_path, False))

        return environment

    # utility methods that tests can use to access the current objects
    def target(self):
        if not self.dbg:
            raise Exception('Invalid debugger instance')
        return self.dbg.GetSelectedTarget()

    def process(self):
        if not self.dbg:
            raise Exception('Invalid debugger instance')
        return self.dbg.GetSelectedTarget().GetProcess()

    def thread(self):
        if not self.dbg:
            raise Exception('Invalid debugger instance')
        return self.dbg.GetSelectedTarget().GetProcess().GetSelectedThread()

    def frame(self):
        if not self.dbg:
            raise Exception('Invalid debugger instance')
        return self.dbg.GetSelectedTarget().GetProcess().GetSelectedThread().GetSelectedFrame()

    def get_process_working_directory(self):
        '''Get the working directory that should be used when launching processes for local or remote processes.'''
        if lldb.remote_platform:
            # Remote tests set the platform working directory up in TestBase.setUp()
            return lldb.remote_platform.GetWorkingDirectory()
        else:
            # local tests change directory into each test subdirectory
            return os.getcwd() 
    
    def tearDown(self):
        #import traceback
        #traceback.print_stack()

        Base.tearDown(self)

        # Delete the target(s) from the debugger as a general cleanup step.
        # This includes terminating the process for each target, if any.
        # We'd like to reuse the debugger for our next test without incurring
        # the initialization overhead.
        targets = []
        for target in self.dbg:
            if target:
                targets.append(target)
                process = target.GetProcess()
                if process:
                    rc = self.invoke(process, "Kill")
                    self.assertTrue(rc.Success(), PROCESS_KILLED)
        for target in targets:
            self.dbg.DeleteTarget(target)

        # Run global post-flight code, if defined via the config file.
        if lldb.post_flight:
            lldb.post_flight(self)

        del self.dbg

    def switch_to_thread_with_stop_reason(self, stop_reason):
        """
        Run the 'thread list' command, and select the thread with stop reason as
        'stop_reason'.  If no such thread exists, no select action is done.
        """
        from lldbutil import stop_reason_to_str
        self.runCmd('thread list')
        output = self.res.GetOutput()
        thread_line_pattern = re.compile("^[ *] thread #([0-9]+):.*stop reason = %s" %
                                         stop_reason_to_str(stop_reason))
        for line in output.splitlines():
            matched = thread_line_pattern.match(line)
            if matched:
                self.runCmd('thread select %s' % matched.group(1))

    def runCmd(self, cmd, msg=None, check=True, trace=False, inHistory=False):
        """
        Ask the command interpreter to handle the command and then check its
        return status.
        """
        # Fail fast if 'cmd' is not meaningful.
        if not cmd or len(cmd) == 0:
            raise Exception("Bad 'cmd' parameter encountered")

        trace = (True if traceAlways else trace)

        # This is an opportunity to insert the 'platform target-install' command if we are told so
        # via the settig of lldb.lldbtest_remote_sandbox.
        if cmd.startswith("target create "):
            cmd = cmd.replace("target create ", "file ")
        if cmd.startswith("file ") and lldb.lldbtest_remote_sandbox:
            with recording(self, trace) as sbuf:
                the_rest = cmd.split("file ")[1]
                # Split the rest of the command line.
                atoms = the_rest.split()
                #
                # NOTE: This assumes that the options, if any, follow the file command,
                # instead of follow the specified target.
                #
                target = atoms[-1]
                # Now let's get the absolute pathname of our target.
                abs_target = os.path.abspath(target)
                print >> sbuf, "Found a file command, target (with absolute pathname)=%s" % abs_target
                fpath, fname = os.path.split(abs_target)
                parent_dir = os.path.split(fpath)[0]
                platform_target_install_command = 'platform target-install %s %s' % (fpath, lldb.lldbtest_remote_sandbox)
                print >> sbuf, "Insert this command to be run first: %s" % platform_target_install_command
                self.ci.HandleCommand(platform_target_install_command, self.res)
                # And this is the file command we want to execute, instead.
                #
                # Warning: SIDE EFFECT AHEAD!!!
                # Populate the remote executable pathname into the lldb namespace,
                # so that test cases can grab this thing out of the namespace.
                #
                lldb.lldbtest_remote_sandboxed_executable = abs_target.replace(parent_dir, lldb.lldbtest_remote_sandbox)
                cmd = "file -P %s %s %s" % (lldb.lldbtest_remote_sandboxed_executable, the_rest.replace(target, ''), abs_target)
                print >> sbuf, "And this is the replaced file command: %s" % cmd

        running = (cmd.startswith("run") or cmd.startswith("process launch"))

        for i in range(self.maxLaunchCount if running else 1):
            self.ci.HandleCommand(cmd, self.res, inHistory)

            with recording(self, trace) as sbuf:
                print >> sbuf, "runCmd:", cmd
                if not check:
                    print >> sbuf, "check of return status not required"
                if self.res.Succeeded():
                    print >> sbuf, "output:", self.res.GetOutput()
                else:
                    print >> sbuf, "runCmd failed!"
                    print >> sbuf, self.res.GetError()

            if self.res.Succeeded():
                break
            elif running:
                # For process launch, wait some time before possible next try.
                time.sleep(self.timeWaitNextLaunch)
                with recording(self, trace) as sbuf:
                    print >> sbuf, "Command '" + cmd + "' failed!"

        if check:
            self.assertTrue(self.res.Succeeded(),
                            msg if msg else CMD_MSG(cmd))

    def match (self, str, patterns, msg=None, trace=False, error=False, matching=True, exe=True):
        """run command in str, and match the result against regexp in patterns returning the match object for the first matching pattern

        Otherwise, all the arguments have the same meanings as for the expect function"""

        trace = (True if traceAlways else trace)

        if exe:
            # First run the command.  If we are expecting error, set check=False.
            # Pass the assert message along since it provides more semantic info.
            self.runCmd(str, msg=msg, trace = (True if trace else False), check = not error)

            # Then compare the output against expected strings.
            output = self.res.GetError() if error else self.res.GetOutput()

            # If error is True, the API client expects the command to fail!
            if error:
                self.assertFalse(self.res.Succeeded(),
                                 "Command '" + str + "' is expected to fail!")
        else:
            # No execution required, just compare str against the golden input.
            output = str
            with recording(self, trace) as sbuf:
                print >> sbuf, "looking at:", output

        # The heading says either "Expecting" or "Not expecting".
        heading = "Expecting" if matching else "Not expecting"

        for pattern in patterns:
            # Match Objects always have a boolean value of True.
            match_object = re.search(pattern, output)
            matched = bool(match_object)
            with recording(self, trace) as sbuf:
                print >> sbuf, "%s pattern: %s" % (heading, pattern)
                print >> sbuf, "Matched" if matched else "Not matched"
            if matched:
                break

        self.assertTrue(matched if matching else not matched,
                        msg if msg else EXP_MSG(str, exe))

        return match_object        

    def expect(self, str, msg=None, patterns=None, startstr=None, endstr=None, substrs=None, trace=False, error=False, matching=True, exe=True, inHistory=False):
        """
        Similar to runCmd; with additional expect style output matching ability.

        Ask the command interpreter to handle the command and then check its
        return status.  The 'msg' parameter specifies an informational assert
        message.  We expect the output from running the command to start with
        'startstr', matches the substrings contained in 'substrs', and regexp
        matches the patterns contained in 'patterns'.

        If the keyword argument error is set to True, it signifies that the API
        client is expecting the command to fail.  In this case, the error stream
        from running the command is retrieved and compared against the golden
        input, instead.

        If the keyword argument matching is set to False, it signifies that the API
        client is expecting the output of the command not to match the golden
        input.

        Finally, the required argument 'str' represents the lldb command to be
        sent to the command interpreter.  In case the keyword argument 'exe' is
        set to False, the 'str' is treated as a string to be matched/not-matched
        against the golden input.
        """
        trace = (True if traceAlways else trace)

        if exe:
            # First run the command.  If we are expecting error, set check=False.
            # Pass the assert message along since it provides more semantic info.
            self.runCmd(str, msg=msg, trace = (True if trace else False), check = not error, inHistory=inHistory)

            # Then compare the output against expected strings.
            output = self.res.GetError() if error else self.res.GetOutput()

            # If error is True, the API client expects the command to fail!
            if error:
                self.assertFalse(self.res.Succeeded(),
                                 "Command '" + str + "' is expected to fail!")
        else:
            # No execution required, just compare str against the golden input.
            if isinstance(str,lldb.SBCommandReturnObject):
                output = str.GetOutput()
            else:
                output = str
            with recording(self, trace) as sbuf:
                print >> sbuf, "looking at:", output

        # The heading says either "Expecting" or "Not expecting".
        heading = "Expecting" if matching else "Not expecting"

        # Start from the startstr, if specified.
        # If there's no startstr, set the initial state appropriately.
        matched = output.startswith(startstr) if startstr else (True if matching else False)

        if startstr:
            with recording(self, trace) as sbuf:
                print >> sbuf, "%s start string: %s" % (heading, startstr)
                print >> sbuf, "Matched" if matched else "Not matched"

        # Look for endstr, if specified.
        keepgoing = matched if matching else not matched
        if endstr:
            matched = output.endswith(endstr)
            with recording(self, trace) as sbuf:
                print >> sbuf, "%s end string: %s" % (heading, endstr)
                print >> sbuf, "Matched" if matched else "Not matched"

        # Look for sub strings, if specified.
        keepgoing = matched if matching else not matched
        if substrs and keepgoing:
            for str in substrs:
                matched = output.find(str) != -1
                with recording(self, trace) as sbuf:
                    print >> sbuf, "%s sub string: %s" % (heading, str)
                    print >> sbuf, "Matched" if matched else "Not matched"
                keepgoing = matched if matching else not matched
                if not keepgoing:
                    break

        # Search for regular expression patterns, if specified.
        keepgoing = matched if matching else not matched
        if patterns and keepgoing:
            for pattern in patterns:
                # Match Objects always have a boolean value of True.
                matched = bool(re.search(pattern, output))
                with recording(self, trace) as sbuf:
                    print >> sbuf, "%s pattern: %s" % (heading, pattern)
                    print >> sbuf, "Matched" if matched else "Not matched"
                keepgoing = matched if matching else not matched
                if not keepgoing:
                    break

        self.assertTrue(matched if matching else not matched,
                        msg if msg else EXP_MSG(str, exe))

    def invoke(self, obj, name, trace=False):
        """Use reflection to call a method dynamically with no argument."""
        trace = (True if traceAlways else trace)
        
        method = getattr(obj, name)
        import inspect
        self.assertTrue(inspect.ismethod(method),
                        name + "is a method name of object: " + str(obj))
        result = method()
        with recording(self, trace) as sbuf:
            print >> sbuf, str(method) + ":",  result
        return result

    # =================================================
    # Misc. helper methods for debugging test execution
    # =================================================

    def DebugSBValue(self, val):
        """Debug print a SBValue object, if traceAlways is True."""
        from lldbutil import value_type_to_str

        if not traceAlways:
            return

        err = sys.stderr
        err.write(val.GetName() + ":\n")
        err.write('\t' + "TypeName         -> " + val.GetTypeName()            + '\n')
        err.write('\t' + "ByteSize         -> " + str(val.GetByteSize())       + '\n')
        err.write('\t' + "NumChildren      -> " + str(val.GetNumChildren())    + '\n')
        err.write('\t' + "Value            -> " + str(val.GetValue())          + '\n')
        err.write('\t' + "ValueAsUnsigned  -> " + str(val.GetValueAsUnsigned())+ '\n')
        err.write('\t' + "ValueType        -> " + value_type_to_str(val.GetValueType()) + '\n')
        err.write('\t' + "Summary          -> " + str(val.GetSummary())        + '\n')
        err.write('\t' + "IsPointerType    -> " + str(val.TypeIsPointerType()) + '\n')
        err.write('\t' + "Location         -> " + val.GetLocation()            + '\n')

    def DebugSBType(self, type):
        """Debug print a SBType object, if traceAlways is True."""
        if not traceAlways:
            return

        err = sys.stderr
        err.write(type.GetName() + ":\n")
        err.write('\t' + "ByteSize        -> " + str(type.GetByteSize())     + '\n')
        err.write('\t' + "IsPointerType   -> " + str(type.IsPointerType())   + '\n')
        err.write('\t' + "IsReferenceType -> " + str(type.IsReferenceType()) + '\n')

    def DebugPExpect(self, child):
        """Debug the spwaned pexpect object."""
        if not traceAlways:
            return

        print child

    @classmethod
    def RemoveTempFile(cls, file):
        if os.path.exists(file):
            os.remove(file)
