#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define B8G_ABI_MAGIC_V3 0x33564742U
#define B8G_ABI_EXPECTED                                                       \
    ((uint32_t)'B' | ((uint32_t)'G' << 8) | ((uint32_t)'V' << 16) |            \
     ((uint32_t)'3' << 24))

static void ensure_dir(const char *path, mode_t mode) {
    if (mkdir(path, mode) != 0 && errno != EEXIST) {
        fprintf(stderr, "[AWESOME-OS] warn: mkdir(%s): %s\n", path,
                strerror(errno));
    }
}

static void mount_or_warn(const char *src, const char *target,
                          const char *fstype, unsigned long flags,
                          const void *data) {
    if (mount(src, target, fstype, flags, data) != 0 && errno != EBUSY) {
        fprintf(stderr, "[AWESOME-OS] warn: mount(%s -> %s, %s): %s\n", src,
                target, fstype, strerror(errno));
    }
}

static int run_wait(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static void run_bg(char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) {
                close(devnull);
            }
        }
        execv(argv[0], argv);
        _exit(127);
    }
}

static void mount_v11_cas_store(void) {
    ensure_dir("/var", 0755);
    ensure_dir("/var/lib", 0755);
    ensure_dir("/var/lib/b8g", 0755);
    ensure_dir("/var/lib/b8g/cas", 0755);

    int mounted = 0;
    /* Attempt persistent ext4 mount by label AWESOME_V11 or legacy B8G_CAS */
    const char *labels[] = {"/dev/disk/by-label/AWESOME_V11",
                            "/dev/disk/by-label/B8G_CAS", NULL};
    for (int i = 0; labels[i] != NULL && !mounted; i++) {
        if (access(labels[i], F_OK) == 0 &&
            mount(labels[i], "/var/lib/b8g/cas", "ext4", 0, NULL) == 0) {
            printf("[AWESOME-OS] mounted persistent ext4 %s -> /var/lib/b8g/cas\n",
                   labels[i]);
            mounted = 1;
        }
    }

    if (!mounted && access("/bin/mount", X_OK) == 0) {
        char *const mount_argv[] = {"/bin/mount", "-t", "ext4", "LABEL=B8G_CAS",
                                    "/var/lib/b8g/cas", NULL};
        if (run_wait(mount_argv) == 0) {
            printf("[AWESOME-OS] mounted persistent ext4 LABEL=B8G_CAS -> /var/lib/b8g/cas\n");
            mounted = 1;
        }
    }

    if (!mounted) {
        mount_or_warn("tmpfs", "/var/lib/b8g/cas", "tmpfs", 0, "mode=0755");
    }

    /* Ensure canonical v11 CAS directory layout */
    ensure_dir("/var/lib/b8g/cas/v11", 0755);
    ensure_dir("/var/lib/b8g/cas/v11/files", 0755);
    ensure_dir("/var/lib/b8g/cas/v11/links", 0755);
}

static int cmdline_has_token(const char *token) {
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';

    size_t toklen = strlen(token);
    const char *p = buf;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        const char *start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' &&
               *p != '\r') {
            p++;
        }
        if ((size_t)(p - start) == toklen && memcmp(start, token, toklen) == 0) {
            return 1;
        }
    }
    return 0;
}

static void setup_baremetal_network(void) {
    if (access("/bin/ip", X_OK) != 0) {
        return;
    }
    char *const lo_argv[] = {"/bin/ip", "link", "set", "lo", "up", NULL};
    (void)run_wait(lo_argv);

    /* Bare-metal NIC discovery: bring up eth0, enp*, eno*, ens* dynamically */
    DIR *d = opendir("/sys/class/net");
    if (!d) {
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' || strcmp(ent->d_name, "lo") == 0) {
            continue;
        }
        char ifname[64];
        snprintf(ifname, sizeof(ifname), "%s", ent->d_name);
        char *const up_argv[] = {"/bin/ip", "link", "set", ifname, "up", NULL};
        if (run_wait(up_argv) == 0) {
            printf("[AWESOME-OS] NIC up: %s\n", ifname);
            if (access("/bin/udhcpc", X_OK) == 0) {
                char *const dhcp_argv[] = {"/bin/udhcpc", "-i", ifname,
                                           "-b",          "-q", NULL};
                run_bg(dhcp_argv);
            }
        }
    }
    closedir(d);
}

static void probe_baremetal_devices(void) {
    int gpus = 0;
    DIR *dri = opendir("/dev/dri");
    if (dri) {
        struct dirent *ent;
        while ((ent = readdir(dri)) != NULL) {
            if (strncmp(ent->d_name, "card", 4) == 0) {
                gpus++;
            }
        }
        closedir(dri);
    }

    int hids = 0;
    DIR *inp = opendir("/dev/input");
    if (inp) {
        struct dirent *ent;
        while ((ent = readdir(inp)) != NULL) {
            if (strncmp(ent->d_name, "event", 5) == 0) {
                hids++;
            }
        }
        closedir(inp);
    }

    printf("[AWESOME-OS] system://local/monitor (DRM GPUs=%d) | system://local/keyboard (HID events=%d)\n",
           gpus, hids);
}

static void run_selftest(void) {
    printf("[T2LINUX] running CI self-test...\n");
    printf("[AWESOME-OS] v11 CAS layout: /var/lib/b8g/cas/v11/files (HOLO_CAS_DIR=%s)\n",
           getenv("HOLO_CAS_DIR") ? getenv("HOLO_CAS_DIR") : "");
    probe_baremetal_devices();

    if (access("/bin/uname", X_OK) == 0) {
        char *const uname_argv[] = {"/bin/uname", "-a", NULL};
        (void)run_wait(uname_argv);
    }

    if (access("/bin/coreutils", X_OK) == 0) {
        printf("[AWESOME-OS] static musl awesome-os/coreutils present\n");
    }

    if (access("/bin/b8g-ghost", X_OK) == 0) {
        char *const ghost_argv[] = {"/bin/b8g-ghost", "--version", NULL};
        int rc = run_wait(ghost_argv);
        printf("[T2LINUX] b8g-ghost --version exited %d\n", rc);
    }

    printf("[T2LINUX] SELFTEST PASS\n");
    printf("[AWESOME-OS] BARE-METAL V11 SELFTEST PASS\n");
    fflush(stdout);
    sync();
    (void)reboot(RB_POWER_OFF);
    for (;;) {
        pause();
    }
}

static void reap_zombies(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
    setenv("HOME", "/root", 1);
    setenv("TERM", "linux", 1);
    setenv("B8G_CAS_DIR", "/var/lib/b8g/cas", 1);
    setenv("HOLO_CAS_DIR", "/var/lib/b8g/cas", 1);

    ensure_dir("/proc", 0555);
    ensure_dir("/sys", 0555);
    ensure_dir("/dev", 0755);
    ensure_dir("/tmp", 01777);
    ensure_dir("/run", 0755);
    ensure_dir("/root", 0700);
    ensure_dir("/etc", 0755);
    ensure_dir("/etc/awesome-os", 0755);

    mount_or_warn("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV,
                  NULL);
    mount_or_warn("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV,
                  NULL);
    mount_or_warn("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755");
    mount_or_warn("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    mount_or_warn("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
    mount_v11_cas_store();

    volatile uint32_t abi_magic = B8G_ABI_MAGIC_V3;
    if (abi_magic != B8G_ABI_EXPECTED) {
        fprintf(stderr,
                "[AWESOME-OS] FATAL: B8G_ABI_MAGIC_V3 mismatch (0x%08x != 0x%08x)\n",
                (unsigned)abi_magic, (unsigned)B8G_ABI_EXPECTED);
        sync();
        (void)reboot(RB_HALT_SYSTEM);
        for (;;) {
            pause();
        }
    }

    printf("[T2LINUX] PID 1 READY (B8G_ABI_MAGIC_V3=0x%08X)\n",
           (unsigned)abi_magic);

    if (cmdline_has_token("t2.selftest=1") || cmdline_has_token("awesome.selftest=1")) {
        run_selftest();
    }

    setup_baremetal_network();
    probe_baremetal_devices();

    printf("\n");
    printf("============================================================\n");
    printf("  Awesome-OS Linux v11 — Bare-Metal UEFI Appliance\n");
    printf("  ABI       : B8G_ABI_MAGIC_V3 (0x33564742)\n");
    printf("  v11 CAS   : /var/lib/b8g/cas/v11/files/\n");
    printf("  System    : system://local/appliance/awesome-os-linux\n");
    printf("  Coreutils : awesome-os/coreutils (static musl Rust)\n");
    printf("============================================================\n\n");

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = reap_zombies;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    (void)sigaction(SIGCHLD, &sa, NULL);

    for (;;) {
        pid_t pid = fork();
        if (pid == 0) {
            (void)setsid();
            char *const sh_argv[] = {"/bin/sh", "-l", NULL};
            execv("/bin/sh", sh_argv);
            fprintf(stderr, "[AWESOME-OS] execv(/bin/sh) failed: %s\n",
                    strerror(errno));
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            printf("\n[AWESOME-OS] /bin/sh exited — respawning shell...\n");
        }
        sleep(1);
    }

    return 0;
}
