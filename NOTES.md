# make menuconfig
please use it inside the docker to customise the build

# qemu : lest work on this 
root@c9e9dffb6c28:/labs# qemu-system-i386 -version
QEMU emulator version 2.0.0 (Debian 2.0.0+dfsg-2ubuntu1.46), Copyright (c) 2003-2008 Fabrice Bellard

# busybox - uerspace
busybox-1.35.0

# kernel
linux-2.6.39

# gdb  (how we debug the kernel in docker)
root@c8b3e24fc6e7:/labs# gdb --version
GNU gdb (Ubuntu 7.7.1-0ubuntu5~14.04.3) 7.7.1
Copyright (C) 2014 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.  Type "show copying"
and "show warranty" for details.
This GDB was configured as "x86_64-linux-gnu".
Type "show configuration" for configuration details.
For bug reporting instructions, please see:
<http://www.gnu.org/software/gdb/bugs/>.
Find the GDB manual and other documentation resources online at:
<http://www.gnu.org/software/gdb/documentation/>.
For help, type "help".
Type "apropos word" to search for commands related to "word".

# gcc (how we build the kernel, in docker)
root@c8b3e24fc6e7:/labs# gcc --version
gcc (Ubuntu 4.8.4-2ubuntu1~14.04.4) 4.8.4
Copyright (C) 2013 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
root@c8b3e24fc6e7:/labs#
