#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Copyright (C) 2017 IBM
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Author: Malahal Naineni <malahal@us.ibm.com>

import re, sys
import pyparsing as pp
import logging, pprint

LBRACE = pp.Literal("{").suppress()
RBRACE = pp.Literal("}").suppress()
SEMICOLON = pp.Literal(";").suppress()
EQUAL = pp.Literal("=").suppress()
KEY = pp.Word(pp.alphas, pp.alphanums+"_")
VALUE = pp.CharsNotIn(';') # doesn't skip whitespace!
BLOCKNAME = pp.Word(pp.alphas, pp.alphanums+"_")
KEYPAIR = KEY + EQUAL + VALUE + SEMICOLON

# block is recursively defined. It starts with name, left brace, then a
# list of "key value" pairs followed by a list of blocks, and finally
# ends with a right brace. We assume that all "key value" pairs ,if any,
# precede any sub-blocks. Ganesha daemon itself allows "key value" pairs
# after sub-blocks or in between sub-blocks, but we don't allow for
# simplification.
#
# We construct a 3 element list in python for every block! The first
# element is the name of block itself, the second element is the list of
# "key value" pairs, and the last element is the list of sub-block. The
# first element must be the name of the block and the remaining two
# elements could be empty lists! This recursive list is usually named as
# r3.

# block definition for pyparsing.
ppblock = pp.Forward()
KEYPAIR_GROUP = pp.Group(pp.ZeroOrMore(pp.Group(KEYPAIR)))
SUBS_GROUP = pp.Group(pp.ZeroOrMore(pp.Group(ppblock)))
ppblock << BLOCKNAME + LBRACE + KEYPAIR_GROUP + SUBS_GROUP + RBRACE

class BLOCK(object):
    def __init__(self, blocknames):
        self.blocknames = blocknames

    def set_keys(self, s, opairs):
        validate_blocknames(self.blocknames)
        validate_opt_pairs(opairs)
        match = ppblock.parseWithTabs().scanString(s)
        block_found = False
        for ppr, start, end in match:
            if block_match(self.blocknames, ppr[0], ppr[1]):
                block_found = True
                break

        if block_found:
            begin_part = s[:start]
            end_part = s[end:]
            r3_ = ppr.asList()
            logging.debug("%s", pprint.pformat(r3_))
            self.set_process(r3_, self.blocknames, opairs)
            text = r3_to_text(r3_, 0)
            logging.debug("%s", pprint.pformat(text))
            assert text[-1] == "\n"
            if end_part[0] == "\n":
                text = text[:-1] # remove the last new line
        else:
            begin_part = s
            end_part = ""
            r3_ = make_r3(self.blocknames)
            self.set_process(r3_, self.blocknames, opairs)
            text = r3_to_text(r3_, 0)

        return begin_part + text + end_part

    def get_keys(self, s, opair):
        validate_blocknames(self.blocknames)
        match = ppblock.parseWithTabs().scanString(s)
        block_found = False
        text = ""
        for ppr, start, end in match:
            if block_match(self.blocknames, ppr[0], ppr[1]):
                match_text = dict(ppr[1].asList())
                if len(opair) == 0:
                    for key in match_text:
                        text = text + "{:<20}".format(key) + "\t\t" + match_text[key] + "\n"
                    block_found = True
                    break;
                else:
                    if match_text.has_key(opair[0]):
                        text = match_text[opair[0]]
                        block_found = True
                        break;
        if False == block_found:
            text = self.blocknames[0] + " not configured"

        return text

    def del_keys(self, s, okeys):
        validate_blocknames(self.blocknames)
        validate_opt_keys(okeys)
        match = ppblock.parseWithTabs().scanString(s)
        block_found = False
        for ppr, start, end in match:
            if block_match(self.blocknames, ppr[0], ppr[1]):
                block_found = True
                break

        if block_found:
            begin_part = s[:start]
            end_part = s[end:]
            r3_ = ppr.asList()
            logging.debug("%s", pprint.pformat(r3_))
            self.del_process(r3_, self.blocknames, okeys)
            text = r3_to_text(r3_, 0)
            logging.debug("%s", pprint.pformat(text))

            # if we remove this entire block, remove the last new line
            # character associated with this block.
            #
            # @todo: should we remove other white space also?
            if end_part[0] == "\n":
                end_part = end_part[1:]
        else:
            logging.debug("block not found")
            sys.exit("block not found")

        return begin_part + text + end_part

    def set_process(self, r3_, blocknames, opairs):
        logging.debug("names: %s, r3: %s", pprint.pformat(blocknames),
                      pprint.pformat(r3_))
        name, pairs, subs = r3_[0], r3_[1], r3_[2]
        assert block_match(blocknames, name, pairs)

        # If last block, add given key value opairs
        subnames = next_subnames(blocknames)
        if not subnames:
            for key, value in opairs:
                key_found = False
                for idx, pair in enumerate(pairs):
                    if key.lower() == pair[0].lower():
                        key_found = True
                        pairs[idx] = [key, value]
                if not key_found:
                    pairs.append([key, value])
            return

        block_found = False
        for sub in subs:
            name2, pairs2, subs2 = sub[0], sub[1], sub[2]
            if block_match(subnames, name2, pairs2):
                block_found = True
                break

        if block_found:
            self.set_process(sub, subnames, opairs)
        else:
            new_r3 = make_r3(subnames)
            subs.append(new_r3)
            self.set_process(new_r3, subnames, opairs)

    def del_process(self, r3_, blocknames, okeys):
        logging.debug("names: %s, r3: %s", pprint.pformat(blocknames),
                      pprint.pformat(r3_))
        name, pairs, subs = r3_[0], r3_[1], r3_[2]

        assert block_match(blocknames, name, pairs)

        # If last block, delete given okeys
        subnames = next_subnames(blocknames)
        if not subnames:
            for key in okeys:
                key_found = False
                for pair in pairs[:]:
                    if key.lower() == pair[0].lower():
                        key_found = True
                        pairs.remove(pair)
                if not key_found: # @todo: exception to report
                    sys.exit("key to delete is not found")

            # export and client blocks can't exist without some
            # key pairs identifying them. So remove the whole
            # block. @todo: shall we do this for regular blocks
            # also?
            if not pairs and (blocknames[0].lower() == "export" or
                              blocknames[0].lower() == "client"):
                r3_[:] = []
            if not okeys: # remove the whole block
                r3_[:] = []

            return

        block_found = False
        for sub in subs:
            name, keypairs, subs2 = sub[0], sub[1], sub[2]
            if block_match(subnames, name, keypairs):
                block_found = True
                break

        if block_found:
            self.del_process(sub, subnames, okeys)
        else:
            logging.debug("block not found")
            sys.exit("block not found")

# Given a block as recursive 3 element list, and the indentation level,
# produce a corresponding text that can be written to config file!
def r3_to_text(r3_, level):
    logging.debug("%s", pprint.pformat(r3_))
    if not r3_:
        return ""
    name, keypairs, subs = r3_[0], r3_[1], r3_[2]
    indent = level * "\t"
    ss_ = indent + name + " {\n"
    for keypair in keypairs:
        key, value = keypair[0], keypair[1]
        ss_ += indent + "\t" + "%s = %s;\n" % (key, value.strip())
    for sub in subs:
        ss_ += r3_to_text(sub, level+1)
    ss_ += indent + "}\n"
    return ss_

# Exception for arguments and options
class ArgError(Exception):
    def __init__(self, error):
        self.error = error

def validate_key(key):
    # We allow any identifier as a block name or a key name
    # except it can't start with an underscore!
    key_re = re.compile(r"^[a-zA-Z]\w*$")
    if not key_re.search(key):
        raise ArgError("'%s' is not a valid key" % key)

def validate_value(value):
    # value should be any printable but should NOT contain a semicolon
    import string
    for char in value:
        if char not in string.printable:
            raise ArgError("'%s' has non printable characters" % value)
        if char == ';':
            raise ArgError("'%s' has semicolon which is not allowed" % value)

def validate_opt_pairs(opairs):
    for key, value in opairs:
        validate_key(key)
        validate_value(value)

def validate_opt_keys(okeys):
    for key in okeys:
        validate_key(key)

def validate_blocknames(blocknames):
    if not blocknames:
        raise ArgError("no blocknames given")

    while blocknames:
        validate_blockname(blocknames)
        blocknames = next_subnames(blocknames)

def validate_blockname(blocknames):
    name_re = re.compile(r"^[a-zA-Z]\w*$")
    if not name_re.search(blocknames[0]):
        raise ArgError("'%s' is not a valid blockname" % blocknames[0])

    # export and client blocks require a key and a value to identify them
    if blocknames[0].lower() == "export" or blocknames[0].lower() == "client":
        if len(blocknames) < 3:
            err = "'%s' block requires 2 additional arguments" % blocknames[0]
            raise ArgError(err)

        key = blocknames[1]
        value = blocknames[2]
        if blocknames[0].lower() == "export":
            valid_keys = ["export_id", "pseudo", "path"]
        else:
            valid_keys = ["clients"]
        if key.lower() not in valid_keys:
            err = "'%s' is not in %s" % (key, pprint.pformat(valid_keys))
            raise ArgError(err)
        validate_value(value)

def next_subnames(blocknames):
    assert blocknames
    if blocknames[0].lower() == "export" or blocknames[0].lower() == "client":
        # export and client blocks require a key and a value to identify them
        return blocknames[3:]
    else:
        return blocknames[1:]

def block_match(blocknames, name, pairs):
    logging.debug("names:%s, name:%s, pairs:%s",
                  pprint.pformat(blocknames), name, pprint.pformat(pairs))
    if blocknames[0].lower() == "export" or blocknames[0].lower() == "client":
        if blocknames[0].lower() != name.lower():
            return False

        key = blocknames[1]
        value = blocknames[2]

        if blocknames[0].lower() == "export":
            valid_keys = ["export_id", "pseudo", "path"]
        else:
            valid_keys = ["clients"]

        assert key.lower() in valid_keys, "key:%s valid_keys:%s" % (key,
                                                                    pprint.pformat(valid_keys))

        for pair in pairs:
            if pair[0].lower() == key.lower() and pair[1].strip() == value:
                return True
        return False

    # neither export nor client block
    return blocknames[0].lower() == name.lower()

# Make a new r3 list from blockname
def make_r3(blocknames):
    if blocknames[0].lower() == "export" or blocknames[0].lower() == "client":
        pairs = [[blocknames[1], blocknames[2]]]
    else:
        pairs = []

    return [blocknames[0], pairs, []]
