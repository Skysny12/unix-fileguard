#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "file_lock.h"

int main(void) {
    int fd = open("test.txt", O_RDWR | O_CREAT, 0644);

    if (fd == -1) {
        perror("파일 열기 실패");
        return 1;
    }

    printf("PID %ld: 잠금 요청\n", (long)getpid());
    fflush(stdout);

    if (lock_file(fd) == -1) {
        close(fd);
        return 1;
    }

    printf("PID %ld: 잠금 획득!\n", (long)getpid());
    fflush(stdout);

    // 충돌 테스트를 위해 8초간 잠금 유지
    sleep(8);

    if (unlock_file(fd) == -1) {
        close(fd);
        return 1;
    }

    printf("PID %ld: 잠금 해제 완료!\n", (long)getpid());

    close(fd);
    return 0;
}

