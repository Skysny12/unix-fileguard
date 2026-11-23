#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include "file_lock.h"

// 파일 전체에 쓰기 잠금 설정
int lock_file(int fd) {
    struct flock lock = {0};

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    // 먼저 잠금 획득 시도
    if (fcntl(fd, F_SETLK, &lock) == 0) {
        return 0;
    }

    if (errno != EACCES && errno != EAGAIN) {
        perror("파일 잠금 실패");
        return -1;
    }

    printf("다른 프로세스가 사용 중입니다. 대기합니다.\n");
    fflush(stdout);

    // 다른 프로세스가 잠금을 해제할 때까지 대기
    while (fcntl(fd, F_SETLKW, &lock) == -1) {
        if (errno == EINTR) continue;

        perror("파일 잠금 대기 실패");
        return -1;
    }

    return 0;
}

// 파일 잠금 해제
int unlock_file(int fd) {
    struct flock lock = {0};

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &lock) == -1) {
        perror("파일 잠금 해제 실패");
        return -1;
    }

    return 0;
}
