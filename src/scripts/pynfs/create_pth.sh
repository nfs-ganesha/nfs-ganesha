python2 - <<EOF
import sys
sitedir = sys.prefix + "/lib/python" + sys.version[:3] + "/site-packages"
filename = sitedir + "/pynfs.pth"
f = open(filename, "w")
f.write("/usr/lib/pynfs\n")
f.close()
print "Created", filename
EOF

