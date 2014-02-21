Configuration files
===================
These are sample configuration files that the developers have
used in their testing.  Some of them may be out of date given
the usual churn of development.

The most current files, as of the introduction of the new
configuration file infrastructure are:

new_config.conf
This is the top level config file for the new infrastructure.
There are some harmless errors in this file but they are present to
show how the new parser detects them.

export_*.conf
These files are included by new_config.conf.  Each one defines
an export using the VFS fsal.

There is further parser work in progress.  This work will remove
more of the parsing quirks such as the double quotes around
things like comma separated tokens.  These config files will be
updated at that time to illustrate all of the features.
