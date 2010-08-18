#!/usr/bin/env python

import os, glob, sys

FILES = 'trace/*.unparsed'

def strip_zeros(s):
	while len(s) > 0 and s[0] == '0':
		s = s[1:]
	return s

def load_symbols(prog):
	f = os.popen('nm -o ' + prog)
	sym = {}

	for line in f.readlines():
		s = line.split()
		val = '0x' + strip_zeros(s[0].split(':')[1])
		name = s[2]
		sym[val] = name

	f.close()

	return sym

def parse_file(finname, foutname):
	fr = file(finname, 'r')
	fw = file(foutname, 'w')

	s = fr.readline()
	while s != '':
		words = s.rstrip().split(' ')
		for i in range(len(words)):
			if words[i].startswith('0x'):
				words[i] = sym_hash[words[i]]

		fw.write(' '.join(words) + '\n')

		s = fr.readline()

	fr.close()
	fw.close()

sym_hash = load_symbols(sys.argv[1])

for finname in glob.glob(FILES):
	assert finname.endswith('.unparsed')

	foutname = finname.replace('.unparsed', '')
	print foutname

	parse_file(finname, foutname)

	os.unlink(finname)
