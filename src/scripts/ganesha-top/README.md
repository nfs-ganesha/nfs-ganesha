# Ganesha-Top - A tool for the information monitoring like top

An idea that makes people easy to monitor information that included memory, CPU,
MDCache, and Export/Client OP. (Now only support V4 Status)

The whole screen could be split to three parts.

  - Header  - ***Display the general information***
  - Body    - ***Display the goal content (like export/client information)***
  - Footer  - ***Display the simple help information and time***

# Header

In this block we would show the build version/commit of ganesha.
And the memory/CPU usage would be displayed here.
In addition to the total ops (included V4.0/V4.1/V4.2)
and MDCache status would also be displayed.

# Body

Show the full data which includes export status, client status,
and NFSv4 op information.

# Footer

Show the useful information like `q` for quit, `h` for help and current time
