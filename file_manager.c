#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "file_manager.h"
#include "file_lock.h"
#include "history.h"

/*
 * 파일 등록 함수
 * 새로운 파일을 생성하며, 이미 존재하면 등록하지 않는다.
 */
int register_file(const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0644);

    if (fd == -1) {
        perror("파일 등록 실패");
        return -1;
    }

    close(fd);
    return 0;
}

/*
 * 파일 조회 함수
 * 지정한 파일의 내용을 읽어 buffer에 저장한다.
 */
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

        if (n == 0)
            break;

        if (n == -1) {
            if (errno == EINTR)
                continue;

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

/*
 * 파일 수정 함수
 * 파일 잠금 획득 후 기존 내용을 자동 백업하고,
 * 새로운 내용으로 수정한 다음 잠금을 해제한다.
 */
int update_file(const char *filename, const char *content) {
    if (filename == NULL || content == NULL)
        return -1;

    // 백업을 위해 읽기 및 쓰기 모드로 파일 열기
    int fd = open(filename, O_RDWR);

    if (fd == -1) {
        perror("파일 열기 실패");
        return -1;
    }

    // 파일 잠금 획득
    if (lock_file(fd) == -1) {
        close(fd);
        return -1;
    }

    // 파일 수정 전 기존 내용 자동 백업
    if (backup_file(fd, filename) == -1) {
        unlock_file(fd);
        close(fd);
        return -1;
    }

    // 잠금 및 백업 완료 후 기존 내용 삭제
    if (ftruncate(fd, 0) == -1) {
        perror("파일 초기화 실패");
        unlock_file(fd);
        close(fd);
        return -1;
    }

    // 새로운 내용 작성
    size_t len = strlen(content);
    size_t total = 0;

    while (total < len) {
        ssize_t n = write(fd, content + total, len - total);

        if (n == -1 && errno == EINTR)
            continue;

        if (n <= 0) {
            perror("파일 수정 실패");
            unlock_file(fd);
            close(fd);
            return -1;
        }

        total += (size_t)n;
    }

    // 파일 잠금 해제
    if (unlock_file(fd) == -1) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

