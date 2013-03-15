Chroot
======
Enabling chroot will put the Apache process that is handling the request into a chroot jail. 

The usual caveats apply when running a process in a chrooted environment. Here are some things to bear in mind when using chroot with mod_vhostx.

PHP
---

`mail()` function does not work as it depends on the `sendmail` binary being inside the chroot. There are [workarounds](https://www.google.com/search?q=php+mail+chroot) however.

Connecting to localhost MySQL/PostgreSQL should be done via TCP rather than Unix socket

Perl
----
Perl requires all libraries to be visible under the chroot dir e.g. the contents of
```
/usr/share/perl5
/usr/lib64/perl5
```


