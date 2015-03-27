#!/usr/bin/env python
from __future__ import print_function
import sys,json,re

comments={}

while 1:
    line = sys.stdin.readline()
    fileline = sys.stdin.readline()
    if not fileline.strip():
        break
    while 1:
        newline = sys.stdin.readline()
        if not newline.strip():
            break
        line += newline

    filere = re.search('FILE: (.*):([0-9]*):', fileline)
    if comments.has_key(filere.group(1)):
        comments[filere.group(1)] += [ { 'line': filere.group(2), 'message': line.strip()} ]
    else:
        comments[filere.group(1)] = [ { 'line': filere.group(2), 'message': line.strip()} ]

if comments:
    #output = { 'comments': comments, 'message': line.strip(), 'labels': {'Code-Review': -1 }}
    output = { 'comments': comments, 'message': "Checkpatch %s" % (line.strip()) }
else:
    #output = {'message': 'Checkpatch OK', 'labels': {'Code-Review': +1 }}
    output = {'message': 'Checkpatch OK'}

print(json.dumps(output))
