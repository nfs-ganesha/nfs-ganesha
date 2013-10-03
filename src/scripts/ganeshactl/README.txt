This README is addressed to developers who plan to either improve this
reference implementation of DBus control for Ganesha or to use it as
a reference for their own implementation.  Those who are looking for
information about setting up DBus and Ganesha in a running system should
consult the installation and administration pages of the project wiki.

This directory contains a reference implementation of tools for managing
NFS Ganesha through DBus.  Everything here is in Python 2.7+ and PyQt 4.2+.

Ganeshactl is a PyQt GUI application for managing NFS Ganesha through DBus.  There
are also python scripts that can be called from the shell to do most of the
management functions.  Under both is a set of python classes that implement the
details of the DBus transactions.

The GUI is a basic Qt application.  I'm sure the useability people will be all
over this thing complaining about my poor design taste and useability ignorance.
Note two things.  First, this is a reference implementation to have a functioning
set of core classes.  Second, see the next section, make it useable according
to your designer sensibilities, and commit the resulting .ui file(s).

Editing the UI
--------------
The UI files (in python) are generated from XML files that are created by
"designer-qt4".  DO NOT edit the .py files directly.  Only the *.ui files are
committed to git and the .py files are overwritten by the build process.
The process of changing/enhancing the UI follows some simple steps:

1. Use designer-qt4 to edit the .ui file.  This tool will manage all of the
   details like buttons and dialog boxes.

2. Generate the corresponding .py file with the 'pyuic4' tool.  This takes the
   XML that describes the UI and generates the .py.

3. Edit ganeshact.py to connect up logic to the UI.  It helps here to examine
   the pyuic4 output.  A good start is to have the new methods display something
   to the status bar.  This ensures that your new GUI's events are wired properly.

4. If you add a new class for a new DBus interface, get an instance of it in
   the main window's __init__ method so it can be referenced properly.

TODO
----
A setup.py and CMakeLists.txt glue has to be put together to create and install
these tools.  When that happens, much of the steps above beyond editing the UI and
adding app logic will be automated.



