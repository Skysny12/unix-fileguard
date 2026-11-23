#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "file_manager.h"

// 새로운 파일 등록
int register_file(const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0644);

    if (fd == -1) {
        perror("파일 등록 실패");
        return -1;
    }

    close(fd);
    return 0;
}

// 파일 내용 조회
int read_file(const char *filename, char *buffer, size_t size) {
    if (size == 0) {
        return -1;
    }

    int fd = open(filename, O_RDONLY);

    if (fd == -1) {
        perror("파일 열기 실패");
        return -1;
    }

    ssize_t total = 0;

    while ((size_t)total < size - 1) {
        ssize_t n = read(fd, buffer + total,
                         size - 1 - (size_t)total);

        if (n == 0) break;

        if (n == -1) {
            if (errno == EINTR) continue;
            perror("파일 읽기 실패");
            close(fd);
            return -1;
        }

        total += n;
    }

    buffer[total] = '\0';
    close(fd);

    return (int)total;
}

