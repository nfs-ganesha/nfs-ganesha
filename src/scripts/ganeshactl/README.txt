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

Script Runtime
--------------
The python "distutils" packaging is now in place to make these scripts installable.
However, there are some limitations that users/developers should be aware of.

1. The scripts work properly if they are invoked from src/scripts/ganeshactl

2. They will also work properly if installed by the RPM tools.

3. They will _not_ work if installed in /usr/local without intervention.

The issue here is how Python manages how it finds its modules.  The following
script fragment illustrates the issue:

       from Ganesha.export_mgr import ExportMgr

This tells the python interpreter to search for the module in its "known" locations
in the filesystem.  The python developers have made some infrastructure design
decisions that make the mix of system packaging, such as RPM or Debian difficult.
The built-in assumption in Python that its packages and the interpreter are in
the same directory tree makes the mix of '/usr/local' and distribution packaging
incompatible without intervention.

The case for (1) works because python does have a search order, i.e. first search
the directory tree where the script is located followed by the system location
which, in the case of RPM would be /usr.

The case for (2) works the same way because the Ganesha modules would be installed
in the system location.

The case for (3) breaks because python cannot find the modules after looking in
the script directory tree and the system location.  Note that one cannot invoke
a script in /usr/local/bin by its full path.  This is because python expects a
different directory structure in the local directory case.  This is confusing
and broken but there has been lots of discussion on the topic over the years but
this is one of the things that are baked-in deeply in the python ecosystem.

For those users/developers who install NFS-Ganesha from source and install in
/usr/local, some extra, manual effort is required.  This is not really a
satisfactory solution, as noted in the python forums and listserv archives, but
it is a workaround for those who don't really want to carve up the python code
or their python installation.  Do the following in the shell or shell scripts:

# export PYTHONPATH=/usr/local/lib/python2.7/site-packages
# manage_exports display 79

This shell fragment sets up the environment to run these scripts located in
/usr/local/bin.  There are side-effects of setting PYTHONPATH in the environment
and if one wants a further explanation of all this, do a search for "PYTHONPATH".
There is a lot more detail in the forums than can be condensed into this README.




