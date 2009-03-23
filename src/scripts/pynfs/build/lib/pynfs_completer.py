
# pynfs - Python NFS4 tools
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

import readline
import __builtin__
import __main__

#
# Customized rlcompleter, which supports completing at end of brackets, []. 
#
import rlcompleter
class Completer(rlcompleter.Completer):
    def attr_matches(self, text):
        """Compute matches when text contains a dot.

        Assuming the text is of the form NAME.NAME....[NAME], and is
        evaluatable in the globals of __main__, it will be evaluated
        and its attributes (as revealed by dir()) are used as possible
        completions.  (For class instances, class members are are also
        considered.)

        WARNING: this can still invoke arbitrary C code, if an object
        with a __getattr__ hook is evaluated.

        """
        import re
        brack_pattern = r"(?:\[.*\])?"
        m = re.match(r"(\w+" + brack_pattern + r"(\.\w+" + brack_pattern + \
                     r")*)\.(\w*)", text)

        if not m:
            return
        expr, attr = m.group(1, 3)
        object = eval(expr, __main__.__dict__)
        words = dir(object)
        if hasattr(object,'__class__'):
            words.append('__class__')
            words = words + rlcompleter.get_class_members(object.__class__)
        matches = []
        n = len(attr)
        for word in words:
            if word[:n] == attr and word != "__builtins__":
                matches.append("%s.%s" % (expr, word))
        return matches


readline.set_completer(Completer().complete)
readline.parse_and_bind("tab: complete")
readline.set_completer_delims(' \t\n`~!@#$%^&*()-=+{}\\|;:\'",<>/?')


def set_history_file(basename):
    import os
    histfile = os.path.join(os.environ["HOME"], basename)

    # Read history
    try:
        readline.read_history_file(histfile)
    except IOError:
        pass
    # Save history upon exit
    import atexit
    atexit.register(readline.write_history_file, histfile)
    del histfile

