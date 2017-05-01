# doas

This is a port of OpenBSD's doas(1) utility to Linux systems.

## Overview

See this [blog post](http://www.tedunangst.com/flak/post/doas-mastery) by Ted Unangst
(the original author of doas) for an introduction to the doas program.

## Differences

There are some differences between this port of doas and the OpenBSD original:

 - On OpenBSD, doas authenticates using "BSD auth". No such functionality is
   available on Linux systems, so this port of doas authenticates against encrypted
   passwords stored in /etc/shadow. PAM (Pluggable Authentication Modules) are not
   supported nor is it a goal of this port to do so (this port was started in order
   to provide a lightweight sudo(8) alternative for embedded systems, where PAM
   typically is not available).
   
 - On OpenBSD, doas stores persistent authentication tokens (configured using the
   `persist` keyword in the configuration file) using an OpenBSD-specific interface
   (namely an ioctl(2) invoked on the controlling terminal). This port of doas stores
   authentication tokens as a combination of a file composed of the invoking user's
   name, the path of the current controlling terminal and doas's parent process id
   and the modification time of the file. These files are only readable and writable
   by root and are stored in a directory which is only readable and writable by root.
   
 - On OpenBSD, all of the library functions required by doas are in the standard
   library. This port of doas requires borrowing and adapting some code from OpenBSD
   or compatibility code from portable OpenBSD projects such as OpenSSH or OpenSMTPD
   (see [bsd-compat/](bsd-compat)) as not all the required library functions are
   implemented by [glibc](https://www.gnu.org/software/libc/)
   or [musl libc](https://www.musl-libc.org/). Any code that has been borrowed
   (including the doas source code and man pages) has been prepended with attribution
   and adaption notices (see License information below). (Note:
   using [libbsd](libbsd.freedesktop.org) was briefly considered, however libbsd is
   difficult to compile against musl libc and the code from OpenBSD is permissively
   licensed and therefore easy to include in this port).

 - This port supports a `-v` flag which prints version information about the
   installed copy of doas, including the commit number and abbreviated commit
   hash. This option is not present in OpenBSD (OpenBSD does not internally version
   their programs).

## Caveats

There are, however, some caveats to this port of doas.

 - The system for storing persistent authentication tokens in this port is easier to
   circumvent than that used in the original OpenBSD version as it relies on
   persistent information stored on disk instead of information stored in the running
   kernel. The token timeouts are measured using the modification time of the token
   file, which means that reversing the system clock could make a previously expired
   ticket valid.
   
 - Due to the implementation details of the system for storing persistent
   authentication tokens, old authentication token files will build up over time in
   the storage directory. (This could perhaps be worked around using cron(8) to
   periodically clear the token storage directory).
   
 - The security of the reimplementation of the OpenBSD specific code has not been
   reviewed to the extent of the code which originates from within OpenBSD.

## Building

Issue `make`. If you need or want to use a different compiler (gcc is default) or
specify extra compiler or linker flags, give them as arguments to make, e.g.:

```
make CC=clang CFLAGS='-DDOAS_STATE_DIR=\"/some/where/else\"' LDFLAGS='-static'
```

The available configurable options are:

 - DOAS\_STATE\_DIR: Directory where persistent authentication token files will be
   stored. Default is `/var/lib/doas`. This directory must be owned by user root and
   group root and must only be readable and writable by root. If these conditions are
   not satisfied then doas will silently fail to store persistent authentication
   tokens when configured to do so.

 - DOAS\_PERSIST\_TIMEOUT: When configured to store persistent authentication tokens,
   the number of seconds after successful authentication for which the token will
   remain valid. The default is 300 seconds (five minutes). Changing this option is
   inadvisable, as it makes doas's behaviour inconsistent with that of the OpenBSD
   default.

 - DOAS\_CONF\_FILE: Path to doas's configuration file. Default is `/etc/doas.conf`.

## Installing

The resulting binary must be installed both setuid root and *setgid* root for
persistent authentication tokens to function correctly. If desired, place the
included man pages in the relevant locations. doas may now be configured using the
configuration file - guidance can be found in doas.conf(5) and in Ted Unangst's blog
post referenced above.

## License

The source code files `persist.c`, `persist.h`, `shadowauth.c` and `shadowauth.h` are
Copyright (c) multiplex'd; please see the files for license details.

All other source code files in the top level directory and the man pages are
Copyright (c) Ted Unangst with adaptions by multiplex'd; please see the files for
license details.

All files in the [bsd-compat](bsd-compat) directory are Copyright (c) their
respective authors, with adaptions by multiplex'd; please see the individual files
for license details.


