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
   authentication tokens as a files, whose names are composed of the invoking user's
   name, the path of the current controlling terminal and various other pieces of 
   information including doas's parent process id and the current session start time,
   and which contains the amount of time that has passed since the kernel booted
   (with granularity of one second). These files are only readable and writable by
   root and are stored in a directory which is only readable and writable by root.
   
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

 - On OpenBSD, doas only needs to know that it is running under a tty in order to 
   record a persistent authentication token. This port needs to know under *which* 
   tty it is running in order to record a persistent token. This is achieved by 
   parsing `/proc/[pid]/stat` (see proc(5)). This requires that procfs is mounted
   on `/proc` for persistent authentication tokens to function correctly.

 - This port supports a `-v` flag which prints version information about the
   installed copy of doas, including the commit number and abbreviated commit
   hash. This option is not present in OpenBSD (OpenBSD does not internally version
   their programs).

## Caveats

There are, however, some caveats to this port of doas.

 - The system for storing the authentication tokens (described above) has *some* 
   integrity against old tokens becoming valid after a reboot, however old tokens 
   should be cleared at boot to prevent this. This can be achieved in several ways,
   such as putting the authentication token storage directory in a tmpfs or by running
   an `@reboot` job in cron(8). Additionally, for long running systems, old token files
   may build up over time, in which case it may be desirable to periodically clear 
   these files using cron(8) or similar.

 - The security of the reimplementation of the OpenBSD specific code has not been
   reviewed to the extent of the code which originates from within OpenBSD.

## Building

Issue `make`. If you need or want to use a different compiler (gcc is default) or
specify extra compiler or linker flags, give them as arguments to make, e.g.:

```
make CC=clang CFLAGS='-DDOAS\_STATE\_DIR=\"/some/where/else\"' LDFLAGS='-static'
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
Copyright (c) multiplexd; please see the files for license details.

All other source code files in the top level directory and the man pages are
Copyright (c) Ted Unangst with adaptions by multiplex'd; please see the files for
license details.

All files in the [bsd-compat](bsd-compat) directory are Copyright (c) their
respective authors, with adaptions by multiplex'd; please see the individual files
for license details.


