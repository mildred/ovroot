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

OverlayFS will only mount the root filesystem and not the filesystems mounted below it. For instance, it won't mount `/dev`, `/proc`, `/run` or `/sys`. If you have a separate `/home` mountpoint, it won't be present in the new root. This is a limitation of OverlayFS. If you have any ideas on solving this, I would be glad to hear about them.

I thought of a solution based on what `mount --rbind` does (as opposed to `mount --bind`). And use this as a first layer for OverlayFS. I ended up with tons of filesystems mounted on my system, some recursively. I tried to umount some using `umount -l` (for lazy umount) and I ended up having umounted all the filesystems on my machine. Couldn't even restart properly because the socket to talk to systemd was lost when umounting `/run` (I believe it was `/run`). If you experiment, be careful, and note that restarting will undo the mess you could have created. Note, key binding Alt+SystRq+S will sync your filesystems if you would ever want to pull the plug from under your computer.

Ideas:

- unshare mount namespace
- prepare a temp dir $tmp
- mkdir $tmp/root $tmp/merged
- pivot root $tmp, /root
- mount overlayfs recursive /root,/root/...upper to /merged
- chroot /merged


