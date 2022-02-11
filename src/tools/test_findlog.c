// SPDX-License-Identifier: LGPL-3.0-or-later
// This is not a valid C file, it's just a bunch of Log function examples to test findlog.sh
/* LogTest("/* comment"); */

/*
 * Copyright IBM Corporation, 2011
 *  Contributor: Frank Filz  <ffilz@us.ibm.com>
 *
 *
 * This software is a server that implements the NFS protocol.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/* LogTest("/* multiline comment");
*/

LogTest("tab");

LogTest("space");

// LogTest("// comment");

 // LogTest("2nd // comment");

LogTest("space before semi");

LogTest("space after semi");

LogTest("function with ; in quotes");

LogTest("parm 1", "parm 2");

LogTest("parm 1", "parm 2 with space after semi");

LogTest("parm 1 with ;", "parm 2 with space after semi");

LogTest("parm 1", "parm 2 with ;");

LogTest("parm 1", "parm 2");

LogTest("paren on new line");

LogVal = "dont find me" LogTest("But do find me");

#define MACRO \
  LogWarn(COMPONENT_CONFIG,            \
          "MACRO")

/* expected too much - need to figure out how to fix the script */

#define MACRO \
  LogWarn(COMPONENT_CONFIG,            \
          "MACRO") \
          { }

/* expected too much - need to figure out how to fix the script */

LogComponents[dont find me];
LogAlways("but find me on line 53");
LogTest("and me", string, buff, compare);
LogTest("and finally me");

#define MACRO1                     \
        LogTest("M1 A",  \
                parms);                 \
      LogTest("M1 B", parms); \
  }

#define MACRO2 \
        LogTest("M2 A",  \
                parms);                 \
      LogTest("M2 B", parms); \
      LogTest("M2 C", parms); \
  }

else
if (!STRCMP(key_name, "LogFile not me 1"))
LogFile = "not me 2";
else if (!STRCMP(key_name, "not me 3"))
LogCrit(COMPONENT_CONFIG, "find me 1 on 76", key_name);

else if (!STRCMP(key_name, "LogFile not me 5"))
LogFile = "not me 6";
LogCrit(COMPONENT_CONFIG, "find me 2 on 82", key_name);

(Logtest("in parens"));

LogTest("foo");
if (LogTest("simple if"))
foo;
foo("oops shouldn't pick this up");

if (LogTest("if with braces on same line")) {
	foo;
}
foo("oops shouldn't pick this up");

if (LogTest("if with braces")) {
	foo;
}
foo("oops shouldn't pick this up");

if (LogTest("if with braces 2")) {
	foo();
}
foo("oops shouldn't pick this up");

LogTest("fini");
