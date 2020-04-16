//#define _POSIX_C_SOURCE 200112L // Needed with glibc (e.g., linux).
#define _GNU_SOURCE
#include <sched.h>
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

static char *sh_argv[] = { "/bin/sh", 0 };

int main (int argc, char *argv[]) {
    const char *root = "/";
    const char *relative_workdir = "work";
    const char *relative_upperdir = "upper";
    const char *overlay = 0;
    int verbose = 0;
    int do_chroot = 1;
    int do_chdir = 1;

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
    if(getcwd(newcwd, PATH_MAX) == 0){
        perror("getcwd");
        return 1;
    }

    char **new_argv = argv + 1;

    int uid = getuid(), euid = geteuid();
    unsigned long mount_flags = MS_MGC_VAL|MS_REC;
    if(uid != 0) mount_flags |= MS_NOSUID;

    int i;
    for(i = 1; i < argc; ++i) {
      new_argv++;
      if (!strcmp(argv[i], "-r")) {
        root = argv[++i];
        new_argv++;
      } else if (!strcmp(argv[i], "-w")) {
        relative_workdir = argv[++i];
        new_argv++;
      } else if (!strcmp(argv[i], "-u")) {
        relative_upperdir = argv[++i];
        new_argv++;
      } else if (!strcmp(argv[i], "-v")) {
        verbose++;
      } else if (!strcmp(argv[i], "--no-chroot")) {
        do_chroot = 0;
      } else if (!strcmp(argv[i], "--no-chdir")) {
        do_chdir = 0;
      } else {
        overlay = argv[i];
        break;
      }
    }

    if(!new_argv[0]) new_argv = sh_argv;

    if(!overlay) {
      fprintf(stderr, "usage: ovroot [--no-chroot] [--no-chdir] [-r ROOT] [-w RELATIVE_WORKDIR] [-u RELATIVE_UPPERDIR] OVERLAY [COMMAND...]\n");
      fprintf(stderr, "UID: %d Effective UID: %d\n", uid, euid);
      if(mount_flags & MS_NOSUID) {
        fprintf(stderr, "Filesystem will be mounted nosuid (you are not root)\n", uid, euid);
      }
      return 1;
    }

    if(verbose) fprintf(stderr, "Root:    %s\n", root);
    if(verbose) fprintf(stderr, "Overlay: %s\n", overlay);
    if(verbose) fprintf(stderr, "Tmpfs:   %s\n", tmpdir);

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

    if(unshare(CLONE_NEWNS) == -1) {
      perror("unshare(CLONE_NEWNS)");
      return 1;
    }

    if(mount(0, "/", 0, MS_SLAVE|MS_REC, 0) == -1) {
      perror("mount make rslave / failed");
      return 1;
    }

    if(mount(overlay_absolute, tmpdir, 0, MS_MGC_VAL|MS_BIND|MS_REC, 0) == -1) {
      char errmsg[PATH_MAX*2+128];
      snprintf(errmsg, PATH_MAX+128, "mount rbind %s to %s failed", overlay_absolute, tmpdir);
      perror(errmsg);
      return 1;
    }

    if(mount(0, tmpdir, 0, MS_SLAVE|MS_REC, 0) == -1) {
      char errmsg[PATH_MAX+128];
      snprintf(errmsg, PATH_MAX+128, "mount make rslave %s failed", tmpdir);
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
    snprintf(tmpdir_work, PATH_MAX, "%s/%s", tmpdir, relative_workdir);
    mkdir(tmpdir_work, 0700);

    char tmpdir_root[PATH_MAX];
    snprintf(tmpdir_root, PATH_MAX, "%s/root", tmpdir);
    mkdir(tmpdir_root, 0700);

    char tmpdir_upper[PATH_MAX];
    snprintf(tmpdir_upper, PATH_MAX, "%s/%s", tmpdir, relative_upperdir);
    mkdir(tmpdir_upper, 0700);

    char tmpdir_merged[PATH_MAX];
    snprintf(tmpdir_merged, PATH_MAX, "%s/merged", tmpdir);
    mkdir(tmpdir_merged, 0700);

    long mount_len = PATH_MAX * 3 + 1024;
    char mount_opts[mount_len];
    snprintf(mount_opts, mount_len, "lowerdir=%s,upperdir=%s,workdir=%s",
      tmpdir_root, tmpdir_upper,
      tmpdir_work);

    while(umount(tmpdir_merged) != -1);

    if(mount(root_absolute, tmpdir_root, 0, MS_MGC_VAL|MS_BIND|MS_REC, 0) == -1) {
      perror("Failed to rbind mount the root filesystem");
      return 1;
    }

    if(mount(0, tmpdir_root, 0, MS_SLAVE|MS_REC, 0) == -1) {
      char errmsg[PATH_MAX+128];
      snprintf(errmsg, PATH_MAX+128, "mount make rslave %s failed", tmpdir_root);
      perror(errmsg);
      return 1;
    }

    if(mount("overlay", tmpdir_merged, "overlay", mount_flags, mount_opts) == -1) {
      char errmsg[PATH_MAX*6+256];
      snprintf(errmsg, PATH_MAX*6+256, "mount(overlay, %s, overlay, %ud, %s)", tmpdir_merged, mount_flags, mount_opts);
      perror(errmsg);
      return 1;
    }

    if(do_chroot) {
        if(chroot(tmpdir_merged) == -1) {
          char errmsg[PATH_MAX+32];
          snprintf(errmsg, PATH_MAX+32, "chroot(%s)", tmpdir_merged);
          perror(errmsg);
          goto cleanup_mount;
        }

        chdir("/");

        if(do_chdir){
            if(chdir(newcwd) == -1) {
              char errmsg[PATH_MAX+32];
              snprintf(errmsg, PATH_MAX+32, "chdir(%s)", newcwd);
              perror(errmsg);
              goto cleanup_mount;
            }
        }
    } else if (do_chdir){
        if(chdir(tmpdir_merged) == -1) {
          char errmsg[PATH_MAX+32];
          snprintf(errmsg, PATH_MAX+32, "chdir(%s)", tmpdir_merged);
          perror(errmsg);
          goto cleanup_mount;
        }
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

    if(umount(tmpdir_root) == -1 && errno != EINVAL) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "umount(%s)", tmpdir_root);
      perror(errmsg);
    }

    if(rmdir(tmpdir) == -1) {
      char errmsg[PATH_MAX+32];
      snprintf(errmsg, PATH_MAX+32, "rmdir(%s)", tmpdir);
      perror(errmsg);
    }

    return 1;
}
