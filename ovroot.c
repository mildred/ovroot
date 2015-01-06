#define _POSIX_C_SOURCE 200112L // Needed with glibc (e.g., linux).
#include <linux/limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mount.h>

#ifndef BIND_ROOT
#define BIND_ROOT 0
#endif

static char *sh_argv[] = { "/bin/sh", 0 };

int main (int argc, char *argv[]) {
    const char *root = "/";
    const char *overlay = 0;
    int verbose = 0;

    char newcwd[PATH_MAX];
    getcwd(newcwd, PATH_MAX);
    
    char **new_argv = argv + 1;
    
    int uid = getuid(), euid = geteuid();
    unsigned long mount_flags = MS_MGC_VAL|MS_REC;
    if(uid != 0) mount_flags |= MS_NOSUID;
    
    int i;
    for(i = 1; i < argc; ++i) {
      new_argv++;
      if (!strcmp(argv[i], "-r")) {
        root = argv[++i];
      } else if (!strcmp(argv[i], "-v")) {
        verbose++;
      } else {
        overlay = argv[i];
        break;
      }
    }
    
    if(!new_argv[0]) new_argv = sh_argv;
    
    if(!overlay) {
      fprintf(stderr, "usage: ovroot [-r ROOT] OVERLAY_ROOT [COMMAND...]\n");
      fprintf(stderr, "UID: %d Effective UID: %d\n", uid, euid);
      if(mount_flags & MS_NOSUID) {
        fprintf(stderr, "Filesystem will be mounted nosuid (you are not root)\n", uid, euid);
      }
      return 1;
    }

    if(verbose) fprintf(stderr, "Root:      %s\n", root);
    if(verbose) fprintf(stderr, "Overlay:   %s\n", overlay);
    
    chdir(overlay);
    char overlay_absolute[PATH_MAX];
    getcwd(overlay_absolute, PATH_MAX);
    char *overlay_name = overlay_absolute;
    for(i = 0; i < PATH_MAX && overlay_absolute[i] != 0; ++i) {
      if(overlay_absolute[i] == '/')
        overlay_name = &overlay_absolute[i+1];
    }
    
    chdir("..");
    
    char cwd[PATH_MAX], workdir[PATH_MAX], mountdir[PATH_MAX], tmpstr[PATH_MAX];
    getcwd(cwd, PATH_MAX);
    strncpy(workdir,  cwd, PATH_MAX);
    strncpy(mountdir, cwd, PATH_MAX);

    snprintf(tmpstr, PATH_MAX, "/.%s_overlayfs_work", overlay_name);
    strncat(workdir, tmpstr, PATH_MAX);
    
    snprintf(tmpstr, PATH_MAX, "/.%s_overlayfs_mount", overlay_name);
    strncat(mountdir, tmpstr, PATH_MAX);
    
#if BIND_ROOT
    char rootbind[PATH_MAX];
    strncpy(rootbind, cwd, PATH_MAX);

    snprintf(tmpstr, PATH_MAX, "/.%s_overlayfs_root", overlay_name);
    strncat(rootbind, tmpstr, PATH_MAX);
    
    if(verbose) fprintf(stderr, "Bind Root: %s\n", rootbind);
#endif
    if(verbose) fprintf(stderr, "Mountdir:  %s\n", mountdir);
    if(verbose) fprintf(stderr, "Workdir:   %s\n", workdir);
    
    mkdir(workdir);
    mkdir(mountdir);
#if BIND_ROOT
    mkdir(rootbind);
#endif
    
    long mount_len = PATH_MAX * 3 + 1024;
    char mount_opts[mount_len];
    snprintf(mount_opts, mount_len, "lowerdir=%s,upperdir=%s,workdir=%s",
#if BIND_ROOT
      rootbind,
#else
      root,
#endif
      overlay_absolute, workdir);
    
    while(umount(mountdir) != -1
#if BIND_ROOT
         || umount(rootbind) != -1
#endif
         );
    
#if BIND_ROOT
    if(mount(root, rootbind, 0, MS_MGC_VAL|MS_BIND|MS_REC, 0) == -1) {
      perror("Failed to rbind mount the root filesystem");
      return 1;
    }
#endif
    
    if(mount("overlay", mountdir, "overlay", mount_flags, mount_opts) == -1) {
      perror("Failed to mount the overlay filesystem");
      return 1;
    }
    
    if(chroot(mountdir) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "chroot(%s)", mountdir);
      perror(errmsg);
      goto cleanup_mount;
    }
    
    chdir("/");
    
    if(chdir(newcwd) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "chdir(%s)", newcwd);
      perror(errmsg);
    }
    
    if(seteuid(uid) == -1) {
      char errmsg[64];
      snprintf(errmsg, 64, "setuid(%d)", uid);
      perror(errmsg);
      goto cleanup_mount;
    }
    
    if(execv(new_argv[0], new_argv) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "execv(%s)", new_argv[0]);
      perror(errmsg);
    }
    
cleanup_mount:
    
    if(umount(mountdir) == -1 && errno != EINVAL) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "umount(%s)", mountdir);
      perror(errmsg);
    }

#if BIND_ROOT
    if(umount(rootbind) == -1 && errno != EINVAL) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "umount(%s)", rootbind);
      perror(errmsg);
    }
#endif

    return 1;
}
