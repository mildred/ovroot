Overlay chroot
==============

This simple tool, designed to be setuid, is able to mount your root filesystem combined with an overlay directory using overlayfs. It then chroot in the created directory and drops you in a shell (or in a command you specified).

Usage
-----

    ovroot [-r ROOT] OVERLAYDIR [COMMAND...]

* `ROOT` defaults to `/`. This is the root which will form the read-only layer of the OverlayFS mount
* `OVERLAYDIR` is a directory forming the read-write layer of the OverlayFS mount
* `COMMAND...` (defaulting to `/bin/sh`) is the command and its arguments to execute in the chroot formed by OverlayFS

Once chrooted in the OverlayFS mounted directory, `ovroot` will change your UID to match what you are (and not keep the root uid inherited from the setuid permission bit) and will try to chdir in the directory matching where you were before calling `ovroot`.

Limitations
-----------

Mount point is not cleaned up after operation.

Fix is to `mount("/", MS_SLAVE|MS_REC)` after unshare
