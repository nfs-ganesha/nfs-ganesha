#!/usr/bin/env python
# rpcgen.py - A Python RPC protocol compiler
# 
# Written by Fred Isaman <iisaman@citi.umich.edu>
# Copyright (C) 2004 University of Michigan, Center for 
#                    Information Technology Integration
#
# Based on version written by Peter Astrand <peter@cendio.se>
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

#
# Note <something>_list means zero or more of <something>.
#
# TODO:
# Code generation for programs and procedures. 
#

##########################################################################
#                                                                        #
# Code is based on the following updated sections of RFCs 1831 and 1832: #
#                                                                        #
##########################################################################

"""
6.2.  Lexical Notes

   (1) Comments begin with '/*' and terminate with '*/'.  (2) White
   space serves to separate items and is otherwise ignored.  (3) An
   identifier is a letter followed by an optional sequence of letters,
   digits or underbar ('_'). The case of identifiers is not ignored.
   (4) A decimal constant expresses a number in base 10, and is a
   sequence of one or more decimal digits, where the first digit is not
   a zero, and is optionally preceded by a minus-sign ('-').  (5) A
   hexadecimal constant expresses a number in base 16, and must be
   preceded by '0x', followed by one or hexadecimal digits ('A', 'B',
   'C', 'D', E', 'F', 'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '2', '3',
   '4', '5', '6', '7', '8', '9'). (6) An octal constant expresses a
   number in base 8, always leads with digit 0, and is a sequence of
   one or more octal digits ('0', '1', '2', '3', '4', '5', '6', '7').


6.3.  Syntax Information

      declaration:
           type-specifier identifier
         | type-specifier identifier "[" value "]"
         | type-specifier identifier "<" [ value ] ">"
         | "opaque" identifier "[" value "]"
         | "opaque" identifier "<" [ value ] ">"
         | "string" identifier "<" [ value ] ">"
         | type-specifier "*" identifier
         | "void"

      value:
           constant
         | identifier

      constant:
         decimal-constant | hexadecimal-constant | octal-constant

      type-specifier:
           [ "unsigned" ] "int"
         | [ "unsigned" ] "hyper"
         | "float"
         | "double"
         | "quadruple"
         | "bool"
         | enum-type-spec
         | struct-type-spec
         | union-type-spec
         | identifier

      enum-type-spec:
         "enum" enum-body

      enum-body:
         "{"
            ( identifier "=" value )
            ( "," identifier "=" value )*
         "}"

      struct-type-spec:
         "struct" struct-body

      struct-body:
         "{"
            ( declaration ";" )
            ( declaration ";" )*
         "}"

      union-type-spec:
         "union" union-body

      union-body:
         "switch" "(" declaration ")" "{"
            case-spec
            case-spec *
            [ "default" ":" declaration ";" ]
         "}"

      case-spec:
        ( "case" value ":")
        ( "case" value ":") *
        declaration ";"

      constant-def:
         "const" identifier "=" constant ";"

      type-def:
           "typedef" declaration ";"
         | "enum" identifier enum-body ";"
         | "struct" identifier struct-body ";"
         | "union" identifier union-body ";"

      definition:
           type-def
         | constant-def

      specification:
           definition *


6.4.  Syntax Notes

   (1) The following are keywords and cannot be used as identifiers:
   "bool", "case", "const", "default", "double", "quadruple", "enum",
   "float", "hyper", "int", "opaque", "string", "struct", "switch",
   "typedef", "union", "unsigned" and "void".

   (2) Only unsigned constants may be used as size specifications for
   arrays.  If an identifier is used, it must have been declared
   previously as an unsigned constant in a "const" definition.

   (3) Constant and type identifiers within the scope of a specification
   are in the same name space and must be declared uniquely within this
   scope.

   (4) Similarly, variable names must be unique within the scope of
   struct and union declarations. Nested struct and union declarations
   create new scopes.

   (5) The discriminant of a union must be of a type that evaluates to
   an integer. That is, "int", "unsigned int", "bool", an enumerated
   type or any typedefed type that evaluates to one of these is legal.
   Also, the case values must be one of the legal values of the
   discriminant.  Finally, a case value may not be specified more than
   once within the scope of a union declaration.

"""

# Being supplemented by RPC structures:
"""
      program-def:
         "program" identifier "{"
            version-def
            version-def *
         "}" "=" constant ";"

      version-def:
         "version" identifier "{"
             procedure-def
             procedure-def *
         "}" "=" constant ";"

      procedure-def:
         proc-return identifier "(" proc-firstarg
           ("," type-specifier )* ")" "=" constant ";"

      proc-return: "void" | type-specifier

      proc-firstarg: "void" | type-specifier


11.3.  Syntax Notes


   o    The following keywords are added and cannot be used as
        identifiers: "program" and "version";

   o    A version name cannot occur more than once within the scope of a
        program definition. Nor can a version number occur more than
        once within the scope of a program definition.

   o    A procedure name cannot occur more than once within the scope of
        a version definition. Nor can a procedure number occur more than
        once within the scope of version definition.

   o    Program identifiers are in the same name space as constant and
        type identifiers.

   o    Only unsigned constants can be assigned to programs, versions
        and procedures.

   o    Current RPC language compilers do not generally support more
        than one type-specifier in procedure argument lists; the usual
        practice is to wrap arguments into a structure.
"""
# Spec above allows the following problems:
# typedef void; 
# typedef enum <enum_body> ID[5];  <---Not a problem
# typedef enum <enum_body> ID<>;   <---Not a problem

import sys
import keyword
import StringIO
import time
import os
# Allow to be run stright from package
if  __name__ == "__main__":
    if os.path.isfile(os.path.join(sys.path[0], 'lib', 'testmod.py')):
        sys.path.insert(1, os.path.join(sys.path[0], 'lib'))

##########################################################################
#                                                                        #
#                            Lexical analysis                            #
#                                                                        #
##########################################################################

import ply.lex as lex

keywords = ("bool", "case", "const", "default", "double", "quadruple",
            "enum", "float", "hyper", "int", "opaque", "string", "struct",
            "switch", "typedef", "union", "unsigned", "void",
            # RPC specific
            "program", "version")

# Required by lex.  Each token also allows a function t_<token>.
tokens = tuple([t.upper() for t in keywords]) + (
    "ID", "CONST10", "CONST8", "CONST16",
    # ( ) [ ] { } 
    "LPAREN", "RPAREN", "LBRACKET", "RBRACKET", "LBRACE", "RBRACE",
    # ; : < > * = ,
    "SEMI", "COLON", "LT", "GT", "STAR", "EQUALS", "COMMA"
    )

# t_<name> functions are used by lex.  They are called with t.value==<match
# of rule in comment>, and t.type==<name>.  They expect a return value
# with attribute type=<token>

# Tell lexer to ignore Whitespace
t_ignore = " \t"

def t_ID(t):
    r'[A-Za-z][A-Za-z0-9_]*'
    if t.value in keywords:
        t.type = t.value.upper()
    return t

def t_CONST16(t):
    r'0x[0-9a-fA-F]+'
    return t

def t_CONST8(t):
    r'0[0-7]+'
    return t

def t_CONST10(t):
    r'-?(([1-9]\d*)|0)'
    return t

# Tokens
t_LPAREN           = r'\('
t_RPAREN           = r'\)'
t_LBRACKET         = r'\['
t_RBRACKET         = r'\]'
t_LBRACE           = r'\{'
t_RBRACE           = r'\}'
t_SEMI             = r';'
t_COLON            = r':'
t_LT               = r'<'
t_GT               = r'>'
t_STAR             = r'\*'
t_EQUALS           = r'='
t_COMMA            = r','

def t_newline(t):
    r'\n+'
    t.lineno += t.value.count("\n")

# Comments
def t_comment(t):
    r'/\*(.|\n)*?\*/'
    t.lineno += t.value.count('\n')

def t_linecomment(t):
    r'%.*\n'
    t.lineno += 1

def t_error(t):
    print "Illegal character %s at %d type %s" % (repr(t.value[0]), t.lineno, t.type)
    t.skip(1)
    
# Build the lexer
lex.lex(debug=0)


##########################################################################
#                                                                        #
#                          Yacc Parsing Info                             #
#                                                                        #
##########################################################################

def p_specification(t):
    '''specification : definition_list'''

def p_definition_list(t):
    '''definition_list : definition definition_list 
                       | empty'''

def p_definition(t):
    '''definition : constant_def
                  | type_def
                  | program_def'''

def p_constant_def(t):
    '''constant_def : CONST ID EQUALS constant SEMI'''
    global name_dict
    id = t[2]
    value = t[4]
    lineno = t.lineno(1)
    if id_unique(id, 'constant', lineno):
        name_dict[id] = const_info(id, value, lineno)

def p_constant(t):
    '''constant : CONST10
                | CONST8
                | CONST16'''
    if len(t[1]) > 9:
        t[0] = t[1] + 'L'
    else:
        t[0] = t[1]

def p_value(t):
    '''value : constant
             | ID'''
    t[0] = t[1]

def p_optional_value(t):
    '''optional_value : value
                      | empty'''
    # return value or None.
    t[0] = t[1]
    # Note this must be unsigned
    value = t[0]
    if value is None or value[0].isdigit():
        return
    msg = ''
    if value[0] == '-':
        msg = "Can't use negative index %s" % value
    elif value not in name_dict:
        msg = "Can't derefence index %s" % value
    else:
        data = name_dict[value]
        if data.type != 'const':
            msg = "Can't use non-constant %s %s as index" % (data.type, value)
        elif not data.positive:
            msg = "Can't use negative index %s" % value
    if msg:
        global error_occurred
        error_occurred = True
        print "ERROR - %s near line %i" % (msg, t.lineno(1))

def p_type_def_1(t):
    '''type_def : TYPEDEF declaration SEMI'''
    # declarations is a type_info
    d = t[2]
    lineno = t.lineno(1)
    sortno = t.lineno(3) + 0.5
    if d.type == 'void':
        global error_occurred
        error_occurred = True
        print "ERROR - can't use void in typedef at line %i" % lineno
        return
    d.lineno = lineno
    if id_unique(d.id, d.type, lineno):
        if d.type == 'enum':
            info = d.create_enum(lineno, sortno)
        elif d.type == 'struct':
            info = d.create_struct(lineno, sortno)
        elif d.type == 'union':
            info = d.create_union(lineno, sortno)
        else:
            info = d
        name_dict[d.id] = info

def p_type_def_2(t):
    '''type_def : ENUM ID enum_body SEMI'''
    id = t[2]
    body = t[3]
    lineno = t.lineno(1)
    sortno = t.lineno(4) + 0.5
    if id_unique(id, 'enum', lineno):
        name_dict[id] = enum_info(id, body, lineno, sortno)

def p_type_def_3(t):
    '''type_def : STRUCT ID struct_body SEMI'''
    id = t[2]
    body = t[3]
    lineno = t.lineno(1)
    if id_unique(id, 'struct', lineno):
        name_dict[id] = struct_info(id, body, lineno)

def p_type_def_4(t):
    '''type_def : UNION ID union_body SEMI'''
    id = t[2]
    body = t[3]
    lineno = t.lineno(1)
    if id_unique(id, 'union', lineno):
        name_dict[id] = union_info(id, body, lineno)

def p_declaration_1(t):
    '''declaration : type_specifier ID'''
    t[1].id = t[2]
    t[0] = t[1]

def p_declaration_2(t):
    '''declaration : type_specifier ID LBRACKET value RBRACKET
                   | type_specifier ID LT optional_value GT
                   | OPAQUE ID LBRACKET value RBRACKET
                   | OPAQUE ID LT optional_value GT
                   | STRING ID LT optional_value GT'''
    if not isinstance(t[1], type_info):
        t[1] =  type_info(t[1], t.lineno(1))
    t[1].id = t[2]
    t[1].array = True
    if t[3] == '[':
        t[1].fixed = True
    else:
        t[1].fixed = False
    t[1].len = t[4]
    t[0] = t[1]

def p_declaration_3(t):
    '''declaration : type_specifier STAR ID'''
    # encode this as the equivalent 'type_specifier ID LT 1 GT'
    if not isinstance(t[1], type_info):
        t[1] =  type_info(t[1], t.lineno(1))
    t[1].id = t[3]
    t[1].array = True
    t[1].fixed = False
    t[1].len = '1'
    t[0] = t[1]

def p_declaration_4(t):
    '''declaration : VOID'''
    t[0] = type_info(t[1], t.lineno(1))

def p_type_specifier_1(t):
    '''type_specifier : UNSIGNED INT
                      | UNSIGNED HYPER'''
    t[0] = type_info('u' + t[2], t.lineno(1))

def p_type_specifier_2(t):
    '''type_specifier : INT
                      | HYPER
                      | FLOAT
                      | DOUBLE
                      | QUADRUPLE
                      | BOOL
                      | ID
                      | UNSIGNED
                      | enum_type_spec
                      | struct_type_spec
                      | union_type_spec'''
    # FRED - Note UNSIGNED is not in spec
    if isinstance(t[1], type_info):
        t[0] = t[1]
    else:
        t[0] = type_info(t[1], t.lineno(1))

def p_enum_type_spec(t):
    '''enum_type_spec : ENUM enum_body'''
    t[0] = type_info("enum", t.lineno(1), body=t[2])

def p_struct_type_spec(t):
    '''struct_type_spec : STRUCT struct_body'''
    t[0] = type_info("struct", t.lineno(1), body=t[2])

def p_union_type_spec(t):
    '''union_type_spec : UNION union_body'''
    t[0] = type_info("union", t.lineno(1), body=t[2])

def p_union_body(t):
    '''union_body : SWITCH LPAREN declaration RPAREN LBRACE switch_body RBRACE'''
    t[0] = [Case_Spec(['switch'], [t[3]])] + t[6]

def p_switch_body(t):
    '''switch_body : case_spec_list default_declaration'''
    # default_declaration is a list of type_info
    t[0] = t[1] + [Case_Spec(['default'], t[2])]

def p_case_spec(t):
    '''case_spec : case_statement_list declaration SEMI'''
    # Note a declaration is a type_info
    # case_* are both lists of strings (values)
    t[0] = [Case_Spec(t[1], [t[2]])]

def p_nonempty_lists(t):
    '''case_spec_list      : case_spec case_spec_list
                           | case_spec
       case_statement_list : case_statement case_statement_list
                           | case_statement'''
    if len(t) == 2:
        t[0] = t[1]
    else:
        t[0] = t[1] + t[2]

def p_case_statement(t):
    '''case_statement : CASE value COLON'''
    t[0] = [t[2]]

def p_default_declaration_1(t):
    '''default_declaration : empty'''
    t[0] = []

def p_default_declaration_(t):
    '''default_declaration : DEFAULT COLON declaration SEMI'''
    t[0] = [t[3]]

def p_struct_body(t):
    '''struct_body : LBRACE declaration_list RBRACE'''
    # Returns a list of type_info declarations
    t[0] = t[2]

def p_declaration_list_1(t):
    '''declaration_list : declaration SEMI''' 
    t[0] = [t[1]]

def p_declaration_list_2(t):
    '''declaration_list : declaration SEMI declaration_list''' 
    t[0] = [t[1]] + t[3]

def p_enum_body(t):
    '''enum_body : LBRACE enum_constant_list RBRACE'''
    # Returns a list of const_info
    t[0] = t[2] 

def p_enum_constant(t):
    '''enum_constant : ID EQUALS value'''
    global name_dict, error_occurred
    id = t[1]
    value = t[3]
    lineno = t.lineno(1)
    if id_unique(id, 'enum', lineno):
        info = name_dict[id] = const_info(id, value, lineno, enum=True)
        if not (value[0].isdigit() or value[0] == '-'):
            # We have a name instead of a constant, make sure it is defined
            if value not in name_dict:
                error_occurred = True
                print "ERROR - can't derefence %s at line %s" % (value, lineno)
            elif not isinstance(name_dict[value], const_info):
                error_occurred = True
                print "ERROR - reference to %s at line %s is not a constant" %\
                      (value, lineno)
            else:
                info.positive = name_dict[value].positive
        t[0] = [info]
    else:
        t[0] = []

def p_enum_constant_list_1(t):
    '''enum_constant_list : enum_constant'''
    t[0] = t[1]

def p_enum_constant_list_2(t):
    '''enum_constant_list : enum_constant COMMA enum_constant_list'''
    t[0] = t[1] + t[3]

def p_empty(t):
    'empty :'

def p_error(t):
    global error_occurred
    error_occurred = True
    if t:
        print "Syntax error at '%s' (lineno %d)" % (t.value, t.lineno)
    else:
        print "Syntax error: unexpectedly hit EOF"

#
# RPC specific routines follow
#

def p_program_def(t):
    '''program_def : PROGRAM ID LBRACE version_def version_def_list RBRACE EQUALS constant SEMI'''
    print "Ignoring program %s = %s" % (t[2], t[8])
    global name_dict
    id = t[2]
    value = t[8]
    lineno = t.lineno(1)
    if id_unique(id, 'program', lineno):
        name_dict[id] = const_info(id, value, lineno)

def p_version_def(t):
    '''version_def :  VERSION ID LBRACE procedure_def procedure_def_list RBRACE EQUALS constant SEMI'''
    global name_dict
    id = t[2]
    value = t[8]
    lineno = t.lineno(1)
    if id_unique(id, 'version', lineno):
        name_dict[id] = const_info(id, value, lineno)

def p_version_def_list(t):
    '''version_def_list : version_def version_def_list
                        | empty'''

def p_procedure_def(t):
    '''procedure_def : proc_return ID LPAREN proc_firstarg  type_specifier_list RPAREN EQUALS constant SEMI'''
    global name_dict
    id = t[2]
    value = t[8]
    lineno = t.lineno(1)
    if id_unique(id, 'procedure', lineno):
        name_dict[id] = const_info(id, value, lineno)

def p_procedure_def_list(t):
    '''procedure_def_list : procedure_def procedure_def_list
                          | empty'''

def p_proc_return(t):
    '''proc_return : type_specifier
                   | VOID'''

def p_proc_firstarg(t):
    '''proc_firstarg : type_specifier
                     | VOID'''

def p_type_specifier_list(t):
    '''type_specifier_list : COMMA type_specifier type_specifier_list
                           | empty'''
    

##########################################################################
#                                                                        #
#                          Global Variables                              #
#                                                                        #
##########################################################################

error_occurred = False # Parsing of infile status

INDENT = 4 # Number of spaces for each indent level
indent = ' ' * INDENT
indent2 = indent * 2

##########################################################################
#                                                                        #
#                   Helper classes and functions                         #
#                                                                        #
##########################################################################

def id_unique(id, name, lineno):
    """Returns True if id not already used.  Otherwise, invokes error"""
    if id in name_dict:
        global error_occurred
        error_occurred = True
        print "ERROR - %s definition %s at line %s conflicts with %s" % \
              (name, id, lineno, name_dict[id])
        return False
    else:
        return True

class Case_Spec(object):
    def __init__(self, cases, declarations):
        self.cases = cases
        self.declarations = declarations

    def __str__(self):
        return "cases %s: %s" % (self.cases, self.declarations)

class Info(object):
    def __init__(self):
        self.lineno = None
        self.sortno = None
        self.type = None
        self.array = False
        self.parent = False

    def __str__(self):
        return "%s %s at line %s" % (self.type, self.id, self.lineno)

    def __cmp__(self, other):
        """Sort on lineno, but send None to end"""
        # FRED - not used
        if self.lineno == other.lineno == None:
            return 0
        if self.lineno == None:
            return 1
        if other.lineno == None:
            return -1
        if self.lineno < other.lineno:
            return -1
        elif self.lineno == other.lineno:
            return 0
        else:
            return 1

    def __cmp__(self, other):
        """Sort on lineno, but send None to end"""
        if self.sortno < other.sortno:
            return -1
        elif self.sortno == other.sortno:
            return 0
        else:
            return 1

    def const_output(self):
        return None
    
    def type_output(self):
        return None

    def pack_output(Self):
        return None

    def unpack_output(self):
        return None

    def brackets(self):
        if self.array:
            if self.fixed:
                out = "[%s]"
            else:
                out = "<%s>"
            if self.len is None:
                out = out % ''
            else:
                out = out % self.len
        else:
            out = ''
        return out

    def fullname(self, value):
        """Put 'const.' in front if needed"""
        if value[0].isdigit() or value[0]=='-':
            return value
        else:
            return "const." + value

    def typeinit(self, varlist, prefix=indent):
        initargs = ''.join([", %s=None" % var.id for var in varlist])
        initvars = ''.join(["%s%sself.%s = %s\n" % (prefix, indent, var.id, var.id)
                            for var in varlist])
        return "%sdef __init__(self%s):\n%s" % (prefix, initargs, initvars)

##     def typerepr(self, varlist, prefix=indent):
##         indent2 = prefix + indent
##         reprbody = ''.join(["%sif self.%s is not None:\n" \
##                             "%s%sout += ['%s=%%s' %% repr(self.%s)]\n" %
##                             (indent2, var.id, indent2, indent, var.id, var.id)
##                             for var in varlist])
##         return "%sdef __repr__(self):\n%sout = []\n" \
##                "%s%sreturn '%s(%%s)' %% ', '.join(out)\n" % \
##                (prefix, indent2, reprbody, indent2, self.id)

    def typerepr(self, varlist, prefix=indent):
        def special(t):
            if t.type in name_dict:
                p = name_dict[t.type]
                if p.parent and p.type == 'enum':
                    return "const.%s.get(self.%s, self.%s)" % (p.id, t.id, t.id)
            return "repr(self.%s)" % t.id
        indent2 = prefix + indent
        reprbody = ''.join(["%sif self.%s is not None:\n" \
                            "%s%sout += ['%s=%%s' %% %s]\n" %
                            (indent2, var.id, indent2, indent, var.id, special(var))
                            for var in varlist])
        return "%sdef __repr__(self):\n%sout = []\n" \
               "%s%sreturn '%s(%%s)' %% ', '.join(out)\n" % \
               (prefix, indent2, reprbody, indent2, self.id)

    def _array_pack(self, prefix, data='data'):
        info = globals()["%s_info" % self.type]
        if isinstance(self, info):
            newdata = data
        else:
            # is a type_info (thus not a typedef)
            newdata = "%s.%s" % (data, self.id)
        if self.array:
            newdata = data
            subheader = "%sdef pack_one_%s(self, data):\n" % (prefix, self.id) 
            varindent = indent
            if self.fixed:
                array = "%sself.pack_farray(%s, data, pack_one_%s)\n" % \
                        (prefix, self.fullname(self.len), self.id)
            else:
                array = "%sself.pack_array(data, pack_one_%s)\n" % \
                        (prefix, self.id)
                if self.len is not None:
                    limit = "%sif len(data) > %s:\n" \
                            "%s%sraise XDRError, 'array length too long'\n" %\
                            (prefix, self.fullname(self.len), prefix, varindent)
                    array = limit + array
        else:
            subheader = array = varindent = ''
        return prefix+varindent, newdata, subheader, array

    def _array_unpack(self, prefix, data='data'):
        info = globals()["%s_info" % self.type]
        if isinstance(self, info):
            newdata = data
        else:
            # is a type_info (thus not a typedef)
            newdata = "%s.%s" % (data, self.id)
        if self.array:
            subheader = "%sdef unpack_one_%s(self, data):\n" % \
                        (prefix, self.id) 
            varindent = indent
            array = "%s%sreturn data\n" % (prefix, varindent)
            if self.fixed:
                array += "%s%s = self.unpack_farray(%s, unpack_one_%s)\n" % \
                        (prefix, newdata, self.fullname(self.len), self.id)
            else:
                array += "%s%s = self.unpack_array(unpack_one_%s)\n" % \
                         (prefix, newdata, self.id)
                if self.len is not None:
                    limit = "%sif len(%s) > %s:\n" \
                            "%s%sraise XDRError, 'array length too long'\n" %\
                            (prefix, newdata, self.fullname(self.len), prefix, indent)
                    array += limit
            newdata = 'data'
        else:
            subheader = array = varindent = ''
        return prefix+varindent, newdata, subheader, array
        
    def packenum(self, prefix, data='data'):
        prefix, data, subheader, array = self._array_pack(prefix, data)
        varlist = ["const.%s" % l.id for l in self.body]
        check = "%sif %s not in [%s]:\n" \
                "%s%sraise XDRError, 'value=%%s not in enum %s' %% %s\n" % \
                (prefix, data, ', '.join(varlist),
                 prefix, indent, self.id, data)
        pack = check + "%sself.pack_int(%s)\n" % (prefix, data)
        return subheader + pack + array

    def unpackenum(self, prefix, data='data'):
        prefix, data, subheader, array = self._array_unpack(prefix, data)
        varlist = ["const.%s" % l.id for l in self.body]
        check = "%sif %s not in [%s]:\n" \
                "%s%sraise XDRError, 'value=%%s not in enum %s' %% %s\n" % \
                (prefix, data, ', '.join(varlist),
                 prefix, indent, self.id, data)
        unpack = "%s%s = self.unpack_int()\n" % (prefix, data)
        return subheader + unpack + check + array

    def packstruct(self, prefix, data='data'):
        prefix, data, subheader, array = self._array_pack(prefix, data)
        pack = ''.join( [l.packout(prefix, data) for l in self.body] )
        return subheader + pack + array

    def unpackstruct(self, prefix, data='data'):
        prefix, data, subheader, array = self._array_unpack(prefix, data)
        if isinstance(self, struct_info):
            classname = "types.%s" % self.id
        else:
            classname = 'nullclass'
        unpack = "%s%s = %s()\n" % (prefix, data, classname) + \
                 ''.join( [l.unpackout(prefix, data) for l in self.body] )
        return subheader + unpack + array

    def packunion(self, prefix, data='data'):
        prefix, data, subheader, array = self._array_pack(prefix, data)
        switch = self.body[0].declarations[0]
        pack = switch.packout(prefix, data)
        first = ''
        for l in self.body[1:-1]:
            cases = ' or '.join(["%s.%s == %s" %
                                 (data, switch.id, self.fullname(c))
                                 for c in l.cases])
            check = "%s%sif %s:\n" % (prefix, first, cases)
            body = ''.join( [d.packout(prefix + indent, data) \
                             for d in l.declarations] )
            pack += check + body
            first = 'el'
        default = self.body[-1].declarations
        pack += "%selse:\n" % (prefix)
        if default != []:
            pack += default[0].packout(prefix + indent, data)
        else:
            pack += "%s%sraise XDRError, 'bad switch=%%s' %% %s.%s\n" % \
                    (prefix, indent, data, switch.id)
        return subheader + pack + array

    def unpackunion(self, prefix, data='data'):
        prefix, data, subheader, array = self._array_unpack(prefix, data)
        if isinstance(self, union_info):
            classname = "types.%s" % self.id
        else:
            classname = 'nullclass'
        unpack = "%s%s = %s()\n" % (prefix, data, classname)
        switch = self.body[0].declarations[0]
        unpack += switch.unpackout(prefix, data)
        first = ''
        for l in self.body[1:-1]:
            cases = ' or '.join(["%s.%s == %s" %
                                 (data, switch.id, self.fullname(c))
                                 for c in l.cases])
            check = "%s%sif %s:\n" % (prefix, first, cases)
            body = ''.join( [d.unpackout(prefix + indent, data) \
                             for d in l.declarations] )
            if l.declarations[0].id is None:
                 arm = "%s%s%s.arm = None\n" % \
                       (prefix, indent, data)
            else:
                arm = "%s%s%s.arm = %s.%s\n" % \
                      (prefix, indent, data, data, l.declarations[0].id)
            unpack += check + body + arm
            first = 'el'
        default = self.body[-1].declarations
        unpack += "%selse:\n" % (prefix)
        if default != []:
            unpack += ''.join( [d.unpackout(prefix + indent, data) \
                                for d in default] )
            if default[0].id is None:
                 arm = "%s%s%s.arm = None\n" % \
                       (prefix, indent, data)
            else:
                arm = "%s%s%s.arm = %s.%s\n" % \
                      (prefix, indent, data, data, default[0].id)
            unpack += arm
        else:
            unpack += "%s%sraise XDRError, 'bad switch=%%s' %% %s.%s\n" % \
                      (prefix, indent, data, switch.id)
            
        return subheader + unpack + array

    def xdrbody(self, prefix=''):
        """Return xdr code for the body (part between braces) of big 3 types"""
        body = ''
        prefix += indent
        if self.type == 'enum':
            body = ''.join(["%s,\n" % l.xdrout(prefix)
                            for l in self.body[:-1]])
            body += "%s\n" % self.body[-1].xdrout(prefix)
        elif self.type == 'struct':
            body = ''.join(["%s\n" % l.xdrout(prefix)
                            for l in self.body])
        elif self.type == 'union':
            for l in self.body[1:-1]:
                body += ''.join(["%scase %s:\n" % (prefix, case) \
                                 for case in l.cases])
                body += ''.join(["%s\n" % d.xdrout(prefix + indent)
                                 for d in l.declarations])
            if self.body[-1].declarations:
                body += "%sdefault:\n" % prefix + \
                        ''.join(["%s\n" % d.xdrout(prefix + indent)
                                 for d in self.body[-1].declarations])
        return body
            
class const_info(Info):
    """The result of 'CONST ID EQUALS constant SEMI' or inside of enum as
    'ID EQUALS value' """
    def __init__(self, id, value, lineno=None, enum=False):
        self.id = id
        self.value = value
        self.positive = value[0] != '-'
        self.lineno = self.sortno = lineno
        self.type = 'const'
        self.enum = enum
        
    def __repr__(self):
        return "constant %s=%s at line %s" % (self.id, self.value, self.lineno)

    def xdrout(self, prefix=''):
        return "%s%s = %s" % (prefix, self.id, self.value)
    
    def const_output(self):
        return "%s = %s\n" % (self.id, self.value)

class enum_info(Info):
    """The result of 'TYPEDEF ENUM <enum_body> ID <array> SEMI' or
    'ENUM ID <enum_body> SEMI'

    Note that an enum which is not of above form will appear as a type_info
    """
    def __init__(self, id, body, lineno=None, sortno=None):
        self.id = id
        self.body = body # list of const_info
        self.lineno = lineno
        if sortno is None:
            self.sortno = self.lineno
        else:
            self.sortno = sortno
        self.type = 'enum'
        self.array = False
        self.parent = True

    def const_output(self):
        body = ''.join(["%s%s : '%s',\n" % (indent, l.value, l.id)
                        for l in self.body])
        return "%s = {\n%s}\n" % (self.id, body)

    def pack_output(self):
        header = "%sdef pack_%s(self, data):\n" % (indent, self.id)
        return header + self.packenum(indent2)

    def unpack_output(self):
        header = "%sdef unpack_%s(self):\n" % (indent, self.id)
        return header + self.unpackenum(indent2) + \
               "%sreturn data\n" % (indent2)
        
class struct_info(Info):
    """The result of 'TYPEDEF STRUCT <struct_body> ID <array> SEMI' or
    'STRUCT ID <struct_body> SEMI'

    Note that a struct which is not of above form will appear as a type_info
    """
    def __init__(self, id, body, lineno=None, sortno=None):
        self.id = id
        self.body = body # list of type_info declarations
        self.lineno = lineno
        if sortno is None:
            self.sortno = self.lineno
        else:
            self.sortno = sortno
        self.type = 'struct'
        self.array = False
        self.parent = True

    def type_output(self):
        comment = '%s# ' % indent
        xdrbody = self.xdrbody(comment)
        xdrdef = "%sXDR definition:\n%sstruct %s {\n%s%s};\n" % \
                 (comment, comment, self.id, xdrbody, comment)
        varlist = [l for l in self.body if l.type != 'void']
        init = self.typeinit(varlist)
        repr = self.typerepr(varlist)
        return "class %s:\n%s%s\n%s\n" % (self.id, xdrdef, init, repr)

    def pack_output(self):
        header = "%sdef pack_%s(self, data):\n" % (indent, self.id)
        return header + self.packstruct(indent2)
        
    def unpack_output(self):
        header = "%sdef unpack_%s(self):\n" % (indent, self.id)
        return header + self.unpackstruct(indent2) + \
               "%sreturn data\n" % (indent2)

class union_info(Info):
    """The result of 'TYPEDEF UNION <union_body> ID <array> SEMI' or
    'UNION ID <union_body> SEMI'

    Note that a union which is not of above form will appear as a type_info
    """
    def __init__(self, id, body, lineno=None, sortno=None):
        self.id = id
        self.body = body # list of type_info declarations
        self.lineno = lineno
        if sortno is None:
            self.sortno = self.lineno
        else:
            self.sortno = sortno
        self.type = 'union'
        self.array = False
        self.parent = True

    def type_output(self):
        comment = '%s# ' % indent
        xdrbody = self.xdrbody(comment)
        switch = "%s" % self.body[0].declarations[0].xdrout(comment +' '*13)
        xdrdef = "%sXDR definition:\n%sunion %s switch(%s) {\n%s%s};\n" % \
                 (comment, comment, self.id,
                  switch[13+len(comment):].rstrip(';'), xdrbody, comment)
        # FRED - need to detect doubles in varlist
        varlist = []
        for c in self.body[:-1]:
            varlist += [l for l in c.declarations if l.type != 'void']
        init = self.typeinit(varlist)
        repr = self.typerepr(varlist)
        return "class %s:\n%s%s\n%s\n" % (self.id, xdrdef, init, repr)

    def pack_output(self):
        header = "%sdef pack_%s(self, data):\n" % (indent, self.id)
        return header + self.packunion(indent2)

    def unpack_output(self):
        header = "%sdef unpack_%s(self):\n" % (indent, self.id)
        return header + self.unpackunion(indent2) + \
               "%sreturn data\n" % (indent2)

class type_info(Info):
    def __init__(self, type, lineno=None, body=None):
        self.id = None
        self.type = type
        self.lineno = self.sortno = lineno
        self.body = body
        self.array = False
        if self.array:
            self.len = None
            self.fixed = False
        self.parent = False

    def __str__(self):
        return "%s %s at line %s" % (self.type, self.id, self.lineno)

    def __repr__(self):
        return "%s %s at line %s" % (self.type, self.id, self.lineno)

    def create_enum(self, lineno, sortno=None):
        enum = enum_info(self.id, self.body, lineno, sortno)
        return self._add_array(enum)

    def create_struct(self, lineno, sortno=None):
        struct = struct_info(self.id, self.body, lineno, sortno)
        return self._add_array(struct)

    def create_union(self, lineno, sortno=None):
        union = union_info(self.id, self.body, lineno, sortno)
        return self._add_array(union)

    def _add_array(self, x):
        x.array = self.array
        if self.array:
            x.len = self.len
            x.fixed = self.fixed
        return x
        
    def xdrout(self, prefix=''):
        if self.type == 'void':
            return "%svoid;" % prefix
        elif self.type == 'enum':
            body = self.xdrbody(prefix)
            name = "%senum {\n%s%s}" % (prefix, body, prefix)
            
        elif self.type == 'struct':
            body = self.xdrbody(prefix)
            name = "%sstruct {\n%s%s}" % (prefix, body, prefix)
        elif self.type == 'union':
            body = self.xdrbody(prefix)
            switch = "%s" % self.body[0].declarations[0].xdrout(prefix +' '*13)
            name = "%sunion switch(%s) {\n%s%s}" % \
                   (prefix, switch[13+len(prefix):].rstrip(';'), body, prefix)
        else:
            name = prefix + self.type
        return "%s %s%s;" % (name, self.id, self.brackets())

    def packout(self, prefix='', data='data'):
        check = "%sif %s.%s is None:\n" \
                "%s%sraise TypeError, '%s.%s == None'\n" % \
                (prefix, data, self.id, prefix, indent, data, self.id)
        if self.type == 'void':
            return prefix + 'pass\n'
        elif self.type == 'struct':
            return check + self.packstruct(prefix, data)
        elif self.type == 'union':
            return check + self.packunion(prefix, data)
        elif self.type == 'enum':
            return check + self.packenum(prefix, data)
        if not self.array:
            return check + \
                   "%sself.pack_%s(%s.%s)\n" % (prefix, self.type, data, self.id)
        return check + self._pack_array(prefix, "%s.%s" % (data, self.id))

    def unpackout(self, prefix='', data='data'):
        if self.type == 'void':
            return prefix + 'pass\n'
        elif self.type == 'struct':
            return self.unpackstruct(prefix, data)
        elif self.type == 'union':
            return self.unpackunion(prefix, data)
        elif self.type == 'enum':
            return self.unpackenum(prefix, data)
        if not self.array:
            return "%s%s.%s = self.unpack_%s()\n" % \
                   (prefix, data, self.id, self.type)
        return self._unpack_array(prefix, "%s.%s" % (data, self.id))

    def pack_output(self):
        if not self.array:
            return "%spack_%s = pack_%s\n" % (indent, self.id, self.type)
        header = "%sdef pack_%s(self, data):\n" % (indent, self.id)
        return header + self._pack_array(indent2)

    def unpack_output(self):
        if not self.array:
            return "%sunpack_%s = unpack_%s\n" % (indent, self.id, self.type)
        header = "%sdef unpack_%s(self):\n" % (indent, self.id)
        return header + self._unpack_array(indent2) + \
               "%sreturn data\n" % (indent2)

    def _pack_array(self, prefix, data='data'):
        if self.fixed or self.len is None:
            limit = ''
        else:
            limit = "%sif len(%s) > %s:\n%s%sraise XDRError, " \
                    "'array length too long for %s'\n" % \
                    (prefix, data, self.fullname(self.len), prefix, indent, data)
        if self.fixed:
            fixchar = 'f'
            fixnum = "%s, " % self.fullname(self.len)
        else:
            fixchar = fixnum = ''
        if self.type == 'string' or self.type == 'opaque':
            type = self.type
            packer = ''
        else:
            type = 'array'
            packer = ", self.pack_%s" % self.type

        pack = "%sself.pack_%s%s(%s%s%s)\n" % \
               (prefix, fixchar, type, fixnum, data, packer)
        return limit + pack
        
    def _unpack_array(self, prefix, data='data'):
        if self.fixed or self.len is None:
            limit = ''
        else:
            limit = "%sif len(%s) > %s:\n%s%sraise XDRError, " \
                    "'array length too long for %s'\n" % \
                    (prefix, data, self.fullname(self.len), prefix, indent, data)
        if self.fixed:
            fixchar = 'f'
            fixnum = ["%s" % self.fullname(self.len)]
        else:
            fixchar = ''
            fixnum = []
        if self.type == 'string' or self.type == 'opaque':
            type = self.type
            packer = []
        else:
            type = 'array'
            packer = ["self.unpack_%s" % self.type]

        pack = "%s%s = self.unpack_%s%s(%s)\n" % \
               (prefix, data, fixchar, type, ', '.join(fixnum+packer))
        return pack + limit
        
     
        
##########################################################################
#                                                                        #
#                          Main Loop                                     #
#                                                                        #
##########################################################################
name_dict = { } # list of global names seen, to avoid conflict

pack_header = """\
import xdrlib
from xdrlib import Error as XDRError

class nullclass(object):
    pass

"""

known_basics = {"int" : "pack_int",
                #"enum" : "pack_enum", 
                "uint" : "pack_uint",
                "unsigned" : "pack_uint",
                "hyper" : "pack_hyper",
                "uhyper" : "pack_uhyper",
                "float" : "pack_float",
                "double" : "pack_double",
                # Note: xdrlib.py does not have a
                # pack_quadruple currently. 
                "quadruple" : "pack_double", 
                "bool" : "pack_bool",
                "opaque": "pack_opaque",
                "string": "pack_string"}
packer_start = ''.join(["%spack_%s = xdrlib.Packer.%s\n" % (indent, k, v)
                        for k, v in known_basics.items()])

unpacker_start = ''.join(["%sunpack_%s = xdrlib.Unpacker.un%s\n" % (indent, k, v)
                          for k, v in known_basics.items()])

def run(infile, debug=False):
    print "Input file is", infile

    # Create output file names (without .py)
    global constants_file, types_file, packer_file
    name_base = os.path.basename(infile[:infile.rfind(".")])
    constants_file = name_base + "_const"
    types_file = name_base + "_type"
    packer_file = name_base + "_pack"
    print "Will use output files %s.py, %s.py, and %s.py" % \
          (constants_file, types_file, packer_file)

    # Parse the input data with yacc
    global name_dict
    name_dict = {'FALSE' : const_info('FALSE', '0', -2),
                 'TRUE' : const_info('TRUE', '1', -1),
                 }
    f = open(infile)
    data = f.read()
    f.close()
    import ply.yacc as yacc
    yacc.yacc()
    yacc.parse(data, debug=debug)

    if error_occurred:
        print
        print "Error occurred, did not write output files"
        return 1

    comment_string = "# Generated by rpcgen.py from %s on %s\n" % \
                     (infile, time.asctime())
    const_fd = file(constants_file + ".py", "w")
    const_fd.write(comment_string)
    type_fd = file(types_file + ".py", "w")
    type_fd.write(comment_string)
    type_fd.write("import %s as const\n" % constants_file)
    pack_fd = file(packer_file + ".py", "w")
    pack_fd.write(comment_string)
    pack_fd.write("import %s as const\n" % constants_file)
    pack_fd.write("import %s as types\n" % types_file)
    pack_fd.write(pack_header)
    pack_fd.write("class %sPacker(xdrlib.Packer):\n" % name_base.upper())
    pack_fd.write(packer_start)

    type_list = name_dict.values()
    type_list.sort()
    for value in type_list:
        #print value
        output = value.const_output()
        if output is not None:
            #const_fd.write("# **** %s ****\n" % value.id)
            const_fd.write(output)
        output = value.type_output()
        if output is not None:
            #type_fd.write("# **** %s ****\n" % value.id)
            type_fd.write(output)
        output = value.pack_output()
        if output is not None:
            #pack_fd.write("# **** %s ****\n" % value.id)
            pack_fd.write(output)
            pack_fd.write('\n')
    pack_fd.write("class %sUnpacker(xdrlib.Unpacker):\n" % name_base.upper())
    pack_fd.write(unpacker_start)
    for value in type_list:
        output = value.unpack_output()
        if output is not None:
            pack_fd.write(output)
            pack_fd.write('\n')
            
    const_fd.close()
    type_fd.close()
    pack_fd.close()
    return

#
# Section: main
#
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "Usage: %s <filename>" % sys.argv[0]
        sys.exit(1)

    run(sys.argv[1])

# Local variables:
# py-indent-offset: 4
# tab-width: 8
# End:
