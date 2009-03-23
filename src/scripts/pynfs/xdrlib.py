"""Implements (a subset of) Sun XDR -- eXternal Data Representation.

See: RFC 1014

"""

import struct
from types import *

try:
    from cStringIO import StringIO as _StringIO
except ImportError:
    from StringIO import StringIO as _StringIO

__all__ = ["Error", "Packer", "Unpacker", "ConversionError"]

# exceptions
class Error:
    """Exception class for this module. Use:

    except xdrlib.Error, var:
        # var has the Error instance for the exception

    Public ivars:
        msg -- contains the message

    """
    def __init__(self, msg):
        self.msg = msg
    def __repr__(self):
        return repr(self.msg)
    def __str__(self):
        return str(self.msg)

class XDRError(Error):
    pass

class ConversionError(Error):
    pass

def assert_int(x):
    	try:
		val = long(x)
	except:
		badinput = repr(x)
		if len(badinput) > 10: badinput = badinput[0:10] + "..."
		raise XDRError, "Expected int, got %s %s" % (badinput, type(x))
	return val

def assert_long(x):
    	try:
		val = long(x)
	except:
		badinput = repr(x)
		if len(badinput) > 10: badinput = badinput[0:10] + "..."
		raise XDRError, "Expected long, got %s %s" % (badinput, type(x))
	return val

def assert_float(x):
    	try:
		val = float(x)
	except:
		badinput = repr(x)
		if len(badinput) > 10: badinput = badinput[0:10] + "..."
		raise XDRError, "Expected float, got %s %s" % (badinput, type(x))
	return val

def assert_double(x):
    	try:
		val = float(x)
	except:
		badinput = repr(x)
		if len(badinput) > 10: badinput = badinput[0:10] + "..."
		raise XDRError, "Expected double, got %s %s" % (badinput, type(x))
	return val

def assert_list(x):
    	try:
		val = list(x)
	except:
		badinput = repr(x)
		if len(badinput) > 10: badinput = badinput[0:10] + "..."
		raise XDRError, "Expected list, got %s %s" % (badinput, type(x))
	return val

def assert_string(x):
    	try:
		val = str(x)
	except:
		badinput = repr(x)
		if len(badinput) > 10: badinput = badinput[0:10] + "..."
		raise XDRError, "Expected string, got %s %s" % (badinput, type(x))
	return val



class Packer:
    """Pack various data representations into a buffer."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.__buf = _StringIO()

    def get_buffer(self):
        return self.__buf.getvalue()
    # backwards compatibility
    get_buf = get_buffer

    def pack_uint(self, x):
	val = assert_int(x)
	self.__buf.write(struct.pack('>L', val))

    pack_int = pack_uint
    pack_enum = pack_int

    def pack_bool(self, x):
    	try: val = assert_int(x)
	except XDRError, msg: raise XDRError, "pack_bool " + repr(msg)
	if val: self.__buf.write('\0\0\0\1')
        else: self.__buf.write('\0\0\0\0')

    def pack_uhyper(self, x):
    	try:
	    	val = assert_long(x)
	except XDRError, msg:
		raise XDRError, "pack_uhyper " + msg.msg
	self.__buf.write(struct.pack('>Q', val))

    pack_hyper = pack_uhyper

    def pack_float(self, x):
    	try:
		val = assert_float(x)
	except XDRError, msg:
		raise XDRError, "pack_float " + msg
	self.__buf.write(struct.pack('>f', val))

    def pack_double(self, x):
    	try:
		val = assert_double(x)
	except XDRError, msg:
		raise XDRError, "pack_double " + msg
        self.__buf.write(struct.pack('>d', x))

    def pack_fstring(self, n, s):
    	try: n = assert_int(n)
	except XDRError, msg: raise XDRError, "pack_fstring " + msg
	try: s = assert_string(s)
	except XDRError, msg: raise XDRErrorr, "pack_fstring " + msg
	if n < 0:
            raise XDRError, 'pack_fstring size must be nonnegative'
        n = ((n+3)/4)*4
        data = s[:n]
        data = data + (n - len(data)) * '\0'
        self.__buf.write(data)

    pack_fopaque = pack_fstring

    def pack_string(self, s):
    	try: s = assert_string(s)
	except XDRError, msg: raise XDRError, "pack_string " + msg
        n = len(s)
	try: self.pack_uint(n)
	except XDRError, msg: raise XDRError, "pack_string " + msg
        try: self.pack_fstring(n, s)
	except XDRError, msg: raise XDRError, "pack_String " + msg

    pack_opaque = pack_string
    pack_bytes = pack_string

    def pack_list(self, list, pack_item):
    	list = assert_list(list)
        for item in list:
            self.pack_uint(1)
            pack_item(item)
        self.pack_uint(0)

    def pack_farray(self, n, list, pack_item):
    	n = assert_int(n)
	list = assert_list(list)
        if len(list) != n:
            raise XDRError, 'wrong array size'
        for item in list:
            pack_item(item)

    def pack_array(self, list, pack_item):
    	list = assert_list(list)
	n = len(list)
        self.pack_uint(n)
        self.pack_farray(n, list, pack_item)



class Unpacker:
    """Unpacks various data representations from the given buffer."""

    def __init__(self, data):
        self.reset(data)

    def reset(self, data):
        self.__buf = data
        self.__pos = 0

    def get_position(self):
        return self.__pos

    def set_position(self, position):
        self.__pos = position

    def get_buffer(self):
        return self.__buf

    def done(self):
        if self.__pos < len(self.__buf):
            raise XDRError, 'unextracted data remains'

    def unpack_uint(self):
        i = self.__pos
        self.__pos = j = i+4
        data = self.__buf[i:j]
        if len(data) < 4:
            raise XDRError, "Caught end of file, %d characters left." % len(data)
        x = struct.unpack('>L', data)[0]
        try:
            return int(x)
        except OverflowError:
            return x

    def unpack_int(self):
        i = self.__pos
        self.__pos = j = i+4
        data = self.__buf[i:j]
        if len(data) < 4:
            raise XDRError, "Caught end of file, %d characters left." % len(data)
        return struct.unpack('>l', data)[0]

    unpack_enum = unpack_int
    unpack_bool = unpack_int

    def unpack_uhyper(self):
        i = self.__pos
	self.__pos = j = i+8
	data = self.__buf[i:j]
	if len(data) < 8:
		raise XDRError, "Caught end of file, %d caracters left." % len(data)
	try:	
		ret = struct.unpack(">Q", data)[0]
	except Exception, excep:
		raise XDRError, "Caught: %s, data = %s." % (excep, repr(data))
	
	return ret 

    def unpack_hyper(self):
        x = self.unpack_uhyper()
        if x >= 0x8000000000000000L:
            x = x - 0x10000000000000000L
        return x

    def unpack_float(self):
        i = self.__pos
        self.__pos = j = i+4
        data = self.__buf[i:j]
        if len(data) < 4:
            raise XDRError, "Caught end of file, %d characters remaining." % len(data)
        return struct.unpack('>f', data)[0]

    def unpack_double(self):
        i = self.__pos
        self.__pos = j = i+8
        data = self.__buf[i:j]
        if len(data) < 8:
            raise XDRError, "Caught end of file, %d characters remaining." % len(data)
	return struct.unpack('>d', data)[0]

    def unpack_fstring(self, n):
        if n < 0:
            raise XDRError, 'fstring size must be nonnegative'
        i = self.__pos
        j = i + (n+3)/4*4
        if j > len(self.__buf):
            raise XDRError, "Caught end of file, %d characters remaining." % (j-i)
        self.__pos = j
        return self.__buf[i:i+n]

    unpack_fopaque = unpack_fstring

    def unpack_string(self):
	try:
		n = self.unpack_uint()
	except XDRError, msg:
		raise XDRError, "unpack_string: unpacking integer" + msg
        return self.unpack_fstring(n)

    unpack_opaque = unpack_string
    unpack_bytes = unpack_string

    def unpack_list(self, unpack_item):
        list = []
        while 1:
	    try:
	    	x = self.unpack_uint()
	    except XDRError, msg:
	        raise XDRError, "unpack_list: " + msg
            if x == 0: break
            if x != 1:
                raise XDRError, "0 or 1 expected, got %s %s" % ( repr(x), type(x) )
	    try:
            	item = unpack_item()
	    except XDRError, msg:
	    	raise XDRError, "unpack_list: " + msg
            list.append(item)
        return list

    def unpack_farray(self, n, unpack_item):
        list = []
        for i in range(n):
            list.append(unpack_item())
        return list

    def unpack_array(self, unpack_item):
        n = self.unpack_uint()
        return self.unpack_farray(n, unpack_item)


# test suite
def _test():
    p = Packer()
    packtest = [
        (p.pack_uint,    (9,)),
        (p.pack_bool,    (None,)),
        (p.pack_bool,    ('hello',)),
        (p.pack_uhyper,  (45L,)),
        (p.pack_float,   (1.9,)),
        (p.pack_double,  (1.9,)),
        (p.pack_string,  ('hello world',)),
        (p.pack_list,    (range(5), p.pack_uint)),
        (p.pack_array,   (['what', 'is', 'hapnin', 'doctor'], p.pack_string)),
        ]
    succeedlist = [1] * len(packtest)
    count = 0
    for method, args in packtest:
        print 'pack test', count,
        try:
            apply(method, args)
            print 'succeeded'
        except ConversionError, var:
            print 'ConversionError:', var.msg
            succeedlist[count] = 0
        except XDRError, var:
	    print 'Error: ', var
	count = count + 1
    data = p.get_buffer()
    # now verify
    up = Unpacker(data)
    unpacktest = [
        (up.unpack_uint,   (), lambda x: x == 9),
        (up.unpack_bool,   (), lambda x: not x),
        (up.unpack_bool,   (), lambda x: x),
        (up.unpack_uhyper, (), lambda x: x == 45L),
        (up.unpack_float,  (), lambda x: 1.89 < x < 1.91),
        (up.unpack_double, (), lambda x: 1.89 < x < 1.91),
        (up.unpack_string, (), lambda x: x == 'hello world'),
        (up.unpack_list,   (up.unpack_uint,), lambda x: x == range(5)),
        (up.unpack_array,  (up.unpack_string,),
         lambda x: x == ['what', 'is', 'hapnin', 'doctor']),
        ]
    count = 0
    for method, args, pred in unpacktest:
        print 'unpack test', count,
        try:
            if succeedlist[count]:
                x = apply(method, args)
                print pred(x) and 'succeeded' or 'failed', ':', x
            else:
                print 'skipping'
        except ConversionError, var:
            print 'ConversionError:', var.msg
        except XDRError, msg:
	    print 'Error:', var
	count = count + 1


if __name__ == '__main__':
    _test()
