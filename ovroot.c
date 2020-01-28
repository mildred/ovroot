//#define _POSIX_C_SOURCE 200112L // Needed with glibc (e.g., linux).
#include <linux/limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef BIND_ROOT
#define BIND_ROOT 1
#endif

static char *sh_argv[] = { "/bin/sh", 0 };

int main (int argc, char *argv[]) {
    const char *root = "/";
    const char *workdir = 0;
    const char *overlay = 0;
    int verbose = 0;
    int do_chroot = 1;

    char* sys_tmpdir = getenv("TMPDIR");
    char tmpdir_template[PATH_MAX];
    snprintf(tmpdir_template, PATH_MAX, "%s/ovroot.%d.XXXXXX", sys_tmpdir ? sys_tmpdir : "/tmp", getpid());
    char *tmpdir = mkdtemp(tmpdir_template);
    if(tmpdir == 0) {
      char errmsg[PATH_MAX+128];
      snprintf(errmsg, PATH_MAX+128, "mkdtemp(%s)", tmpdir_template);
      perror(errmsg);
      return 1;
    }

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
      } else if (!strcmp(argv[i], "-w")) {
        workdir = argv[++i];
      } else if (!strcmp(argv[i], "-v")) {
        verbose++;
      } else if (!strcmp(argv[i], "--no-chroot")) {
        do_chroot = 0;
      } else {
        overlay = argv[i];
        break;
      }
    }

    if(!new_argv[0]) new_argv = sh_argv;

    if(!overlay || !workdir) {
      fprintf(stderr, "usage: ovroot [--no-chroot] [-r ROOT] -w WORKDIR OVERLAY_ROOT [COMMAND...]\n");
      fprintf(stderr, "UID: %d Effective UID: %d\n", uid, euid);
      if(mount_flags & MS_NOSUID) {
        fprintf(stderr, "Filesystem will be mounted nosuid (you are not root)\n", uid, euid);
      }
      return 1;
    }

    if(verbose) fprintf(stderr, "Root:  %s\n", root);
    if(verbose) fprintf(stderr, "Upper: %s\n", overlay);

    char cwd[PATH_MAX];
    if(getcwd(cwd, PATH_MAX) == 0) perror("getcwd(cwd)");

    char overlay_absolute[PATH_MAX];
    if(realpath(overlay, overlay_absolute) == 0) {
      char errmsg[PATH_MAX+128];
      snprintf(errmsg, PATH_MAX+128, "realpath(%s)", overlay);
      perror(errmsg);
      return 1;
    }

    char root_absolute[PATH_MAX];
    if(realpath(root, root_absolute) == 0) {
      char errmsg[PATH_MAX+128];
      snprintf(errmsg, PATH_MAX+128, "realpath(%s)", root);
      perror(errmsg);
      return 1;
    }

    char workdir_absolute[PATH_MAX];
    if(realpath(workdir, workdir_absolute) == 0) {
      char errmsg[PATH_MAX+128];
      snprintf(errmsg, PATH_MAX+128, "realpath(%s)", workdir);
      perror(errmsg);
      return 1;
    }

    if(verbose) fprintf(stderr, "Tmpfs: %s\n", tmpdir);

    if(mount("tmpfs", tmpdir, "tmpfs", 0, 0) == -1) {
      char errmsg[PATH_MAX+128];
      snprintf(errmsg, PATH_MAX+128, "mount tmpfs to %s failed", tmpdir);
      perror(errmsg);
      return 1;
    }

    if(mount(0, tmpdir, 0, MS_UNBINDABLE, 0) == -1) {
      char errmsg[PATH_MAX+128];
      snprintf(errmsg, PATH_MAX+128, "mount make runbindable %s failed", tmpdir);
      perror(errmsg);
      return 1;
    }

    if(chdir(tmpdir) == -1) {
      char errmsg[PATH_MAX+128];
      snprintf(errmsg, PATH_MAX+128, "chdir(%s)", tmpdir);
      perror(errmsg);
      return 1;
    }

    char tmpdir_work[PATH_MAX];
    snprintf(tmpdir_work, PATH_MAX, "%s/work", tmpdir);
    mkdir(tmpdir_work, 0700);

#if BIND_ROOT
    char tmpdir_root[PATH_MAX];
    snprintf(tmpdir_root, PATH_MAX, "%s/root", tmpdir);
    mkdir(tmpdir_root, 0700);

    char tmpdir_upper[PATH_MAX];
    snprintf(tmpdir_upper, PATH_MAX, "%s/upper", tmpdir);
    mkdir(tmpdir_upper, 0700);
#endif

    char tmpdir_merged[PATH_MAX];
    snprintf(tmpdir_merged, PATH_MAX, "%s/merged", tmpdir);
    mkdir(tmpdir_merged, 0700);

    long mount_len = PATH_MAX * 3 + 1024;
    char mount_opts[mount_len];
    snprintf(mount_opts, mount_len, "lowerdir=%s,upperdir=%s,workdir=%s",
#if BIND_ROOT
      tmpdir_root, tmpdir_upper,
#else
      root_absolute, overlay_absolute,
#endif
      tmpdir_work);

    while(umount(tmpdir_merged) != -1);

#if BIND_ROOT
    if(mount(root_absolute, tmpdir_root, 0, MS_MGC_VAL|MS_BIND|MS_REC, 0) == -1) {
      perror("Failed to rbind mount the root filesystem");
      return 1;
    }

    if(mount(workdir_absolute, tmpdir_work, 0, MS_MGC_VAL|MS_BIND|MS_REC, 0) == -1) {
      perror("Failed to rbind mount the workdir");
      return 1;
    }

    if(mount(overlay_absolute, tmpdir_upper, 0, MS_MGC_VAL|MS_BIND|MS_REC, 0) == -1) {
      perror("Failed to rbind mount the upper filesystem");
      return 1;
    }
#endif

    if(mount("overlay", tmpdir_merged, "overlay", mount_flags, mount_opts) == -1) {
      perror("Failed to mount the overlay filesystem");
      return 1;
    }

    if(chroot(tmpdir_merged) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "chroot(%s)", tmpdir_merged);
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

    if(umount(tmpdir_merged) == -1 && errno != EINVAL) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "umount(%s)", tmpdir_merged);
      perror(errmsg);
    }

#if BIND_ROOT
    if(umount(tmpdir_upper) == -1 && errno != EINVAL) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "umount(%s)", tmpdir_upper);
      perror(errmsg);
    }

    if(umount(tmpdir_root) == -1 && errno != EINVAL) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "umount(%s)", tmpdir_root);
      perror(errmsg);
    }

    if(rmdir(tmpdir_upper) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "rmdir(%s)", tmpdir_upper);
      perror(errmsg);
    }

    if(rmdir(tmpdir_root) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "rmdir(%s)", tmpdir_root);
      perror(errmsg);
    }
#endif

    if(rmdir(tmpdir_work) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "rmdir(%s)", tmpdir_work);
      perror(errmsg);
    }

    if(rmdir(tmpdir_merged) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "rmdir(%s)", tmpdir_merged);
      perror(errmsg);
    }

    if(rmdir(tmpdir) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "rmdir(%s)", tmpdir);
      perror(errmsg);
    }

    return 1;
}
