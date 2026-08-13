#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    printf("PID: %ld\n", (long)getpid());
    printf("Parent PID: %ld\n", (long)getppid());

    int fd = open("/tmp/microproxy_phase02.txt",
                  O_CREAT | O_WRONLY | O_TRUNC,
                  0644);

    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    printf("Opened file descriptor: %d\n", fd);

    const char message[] = "phase02\n";

    ssize_t written = write(fd, message, sizeof(message) - 1);

    if (written == -1) {
        perror("write");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Wrote %zd bytes\n", written);
    printf("Inspect /proc/%ld/fd now.\n", (long)getpid());

    getchar();

    if (close(fd) == -1) {
        perror("close");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
