#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Exit 77 means the kernel/runtime refused the isolation experiment. */
#define SKIP_EXIT 77

static int write_text(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t n = strlen(text);
    ssize_t written = write(fd, text, n);
    int saved = errno;
    if (close(fd) < 0 && written == (ssize_t)n) return -1;
    errno = saved;
    return written == (ssize_t)n ? 0 : -1;
}

static int setup_user_mapping(uid_t host_uid, gid_t host_gid)
{
    char mapping[80];
    /* setgroups must be disabled before writing gid_map as non-root. */
    if (write_text("/proc/self/setgroups", "deny\n") < 0 && errno != ENOENT)
        return -1;
    snprintf(mapping, sizeof(mapping), "0 %lu 1\n", (unsigned long)host_uid);
    if (write_text("/proc/self/uid_map", mapping) < 0) return -1;
    snprintf(mapping, sizeof(mapping), "0 %lu 1\n", (unsigned long)host_gid);
    if (write_text("/proc/self/gid_map", mapping) < 0) return -1;
    return 0;
}

static int wait_for(pid_t child)
{
    int status;
    pid_t result;
    do { result = waitpid(child, &status, 0); }
    while (result < 0 && errno == EINTR);
    if (result < 0) { perror("waitpid"); return 1; }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "child terminated by signal %d\n", WTERMSIG(status));
    }
    return 1;
}

static int isolated_init(void)
{
    if (getpid() != 1) {
        fprintf(stderr, "expected PID 1 inside namespace; observed %ld\n", (long)getpid());
        return 1;
    }
    /* This alters mount propagation only in the new mount namespace. */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
        perror("mount private"); return 1;
    }
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) {
        perror("mount /proc"); return 1;
    }
    struct stat st;
    if (stat("/proc/1", &st) < 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "isolated /proc/1 missing\n"); return 1;
    }
    printf("ISOLATED_INIT_PID=%ld\n", (long)getpid());
    printf("ISOLATED_INIT_PPID=%ld\n", (long)getppid());
    fflush(stdout);

    /* Demonstrate a child that lives under PID 1 in the namespace. */
    pid_t worker = fork();
    if (worker < 0) { perror("fork worker"); return 1; }
    if (worker == 0) {
        printf("ISOLATED_WORKER_PID=%ld PPID=%ld\n", (long)getpid(), (long)getppid());
        fflush(stdout);
        _exit(getpid() == 2 && getppid() == 1 ? 0 : 1);
    }
    return wait_for(worker);
}

static int experiment(void)
{
    /*
     * Capture the host credentials before entering the new user namespace.
     * Until uid_map/gid_map are installed, getuid()/getgid() inside the new
     * namespace may appear as the overflow IDs (commonly 65534).
     */
    uid_t host_uid = getuid();
    gid_t host_gid = getgid();

    if (unshare(CLONE_NEWUSER) < 0) {
        if (errno == EPERM || errno == EINVAL || errno == ENOSYS || errno == EUSERS) {
            fprintf(stderr, "SKIP: user namespaces unavailable: %s\n", strerror(errno));
            return SKIP_EXIT;
        }
        perror("unshare user"); return 1;
    }
    if (setup_user_mapping(host_uid, host_gid) < 0) {
        if (errno == EPERM || errno == EACCES || errno == EROFS) {
            fprintf(stderr, "SKIP: uid/gid mapping unavailable: %s\n", strerror(errno));
            return SKIP_EXIT;
        }
        perror("uid/gid map"); return 1;
    }
    if (unshare(CLONE_NEWNS | CLONE_NEWPID) < 0) {
        if (errno == EPERM || errno == EINVAL || errno == ENOSYS) {
            fprintf(stderr, "SKIP: PID/mount namespaces unavailable: %s\n", strerror(errno));
            return SKIP_EXIT;
        }
        perror("unshare pid/mount"); return 1;
    }
    /* unshare(CLONE_NEWPID) changes PID numbering for future children only. */
    pid_t init = fork();
    if (init < 0) { perror("fork init"); return 1; }
    if (init == 0) _exit(isolated_init());
    return wait_for(init);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: pid_mount_lab [--help]");
        puts("Runs a disposable rootless PID+mount namespace experiment.");
        puts("Exit 0=verified, 77=restricted kernel/runtime, other=failure.");
        return 0;
    }
    if (argc != 1) { fprintf(stderr, "unknown argument\n"); return 2; }
    /* Keep host parent out of all newly created namespaces. */
    pid_t child = fork();
    if (child < 0) { perror("fork sandbox"); return 1; }
    if (child == 0) _exit(experiment());
    int code = wait_for(child);
    if (code == 0) puts("PHASE22_ISOLATION_VERIFIED");
    return code;
}
