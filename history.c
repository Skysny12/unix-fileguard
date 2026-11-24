#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "history.h"
#include "file_lock.h"

/*
 * 파일 수정 전 현재 내용을 버전별로 백업한다.
 * 호출한 프로세스가 이미 파일 잠금을 획득한 상태여야 한다.
 */
int backup_file(int fd, const char *filename) {
    if (filename == NULL || filename[0] == '\0' ||
        strchr(filename, '/') != NULL) {
        fprintf(stderr, "잘못된 파일 이름\n");
        return -1;
    }

    // 백업 디렉터리 생성
    if (mkdir("data", 0755) == -1 && errno != EEXIST) {
        perror("data 디렉터리 생성 실패");
        return -1;
    }

    if (mkdir("data/history", 0755) == -1 &&
        errno != EEXIST) {
        perror("history 디렉터리 생성 실패");
        return -1;
    }

    char path[512];

    // 기존 버전과 겹치지 않는 번호 찾기
    for (int version = 1; version <= 9999; version++) {
        int len = snprintf(path, sizeof(path),
                           "data/history/%s.v%d",
                           filename, version);

        if (len < 0 || (size_t)len >= sizeof(path)) {
            fprintf(stderr, "백업 파일 경로가 너무 김\n");
            return -1;
        }

        // 이미 존재하는 버전은 덮어쓰지 않음
        int out = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);

        if (out == -1 && errno == EEXIST)
            continue;

        if (out == -1) {
            perror("백업 파일 생성 실패");
            return -1;
        }

        char buffer[4096];
        off_t offset = 0;

        // 잠금 중인 원본 fd에서 내용 읽기
        while (1) {
            ssize_t n = pread(fd, buffer, sizeof(buffer), offset);

            if (n == 0)
                break;

            if (n == -1) {
                if (errno == EINTR)
                    continue;
                perror("백업 읽기 실패");
                goto fail;
            }

            offset += n;
            ssize_t written = 0;

            while (written < n) {
                ssize_t w = write(out, buffer + written,
                                  (size_t)(n - written));

                if (w == -1 && errno == EINTR)
                    continue;

                if (w <= 0) {
                    fprintf(stderr, "백업 쓰기 실패\n");
                    goto fail;
                }

                written += w;
            }
        }

        if (close(out) == -1) {
            perror("백업 파일 닫기 실패");
            unlink(path);
            return -1;
        }

        printf("자동 백업 완료: %s\n", path);
        return 0;

fail:
        close(out);
        unlink(path); // 실패한 불완전 백업 제거
        return -1;
    }

    fprintf(stderr, "백업 버전 수 초과\n");
    return -1;
}


/*
 * 파일 변경 이력 조회 함수
 * 저장된 백업 파일을 버전 번호순으로 출력한다.
 */
int list_history(const char *filename) {
    if (filename == NULL || filename[0] == '\0' ||
        strchr(filename, '/') != NULL) {
        fprintf(stderr, "잘못된 파일 이름\n");
        return -1;
    }

    char path[512];
    int count = 0;

    printf("\n=== %s 변경 이력 ===\n", filename);

    // 버전 1부터 순서대로 백업 파일 확인
    for (int version = 1; version <= 9999; version++) {
        int len = snprintf(path, sizeof(path),
                           "data/history/%s.v%d",
                           filename, version);

        if (len < 0 || (size_t)len >= sizeof(path))
            return -1;

        struct stat info;

        if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
            printf("버전 %d: %s (%lld bytes)\n",
                   version, path, (long long)info.st_size);
            count++;
        }
    }

    if (count == 0)
        printf("저장된 변경 이력이 없습니다.\n");

    return count;
}


/*
 * 이전 버전 복구 함수
 * 파일 잠금 후 현재 내용을 백업하고,
 * 선택한 버전의 내용으로 복구한다.
 */
int restore_file(const char *filename, int version) {
    if (filename == NULL || filename[0] == '\0' ||
        strchr(filename, '/') != NULL ||
        version < 1 || version > 9999) {
        fprintf(stderr, "잘못된 파일명 또는 버전\n");
        return -1;
    }

    char path[512];

    int len = snprintf(path, sizeof(path),
                       "data/history/%s.v%d",
                       filename, version);

    if (len < 0 || (size_t)len >= sizeof(path))
        return -1;

    // 복구할 백업 파일 열기
    int src = open(path, O_RDONLY);

    if (src == -1) {
        perror("백업 파일 열기 실패");
        return -1;
    }

    // 현재 파일 열기
    int fd = open(filename, O_RDWR);

    if (fd == -1) {
        perror("원본 파일 열기 실패");
        close(src);
        return -1;
    }

    // 복구 작업 중 다른 프로세스의 수정 방지
    if (lock_file(fd) == -1) {
        close(fd);
        close(src);
        return -1;
    }

    int result = -1;

    // 복구 전 현재 파일 내용도 백업
    if (backup_file(fd, filename) == -1)
        goto cleanup;

    // 기존 내용 삭제
    if (ftruncate(fd, 0) == -1) {
        perror("파일 초기화 실패");
        goto cleanup;
    }

    if (lseek(fd, 0, SEEK_SET) == -1) {
        perror("파일 위치 이동 실패");
        goto cleanup;
    }

    char buffer[4096];

    // 백업 파일 내용을 현재 파일로 복사
    while (1) {
        ssize_t n = read(src, buffer, sizeof(buffer));

        if (n == 0)
            break;

        if (n == -1) {
            if (errno == EINTR)
                continue;
            perror("백업 읽기 실패");
            goto cleanup;
        }

        ssize_t total = 0;

        while (total < n) {
            ssize_t w = write(fd, buffer + total,
                              (size_t)(n - total));

            if (w == -1 && errno == EINTR)
                continue;

            if (w <= 0) {
                perror("복구 쓰기 실패");
                goto cleanup;
            }

            total += w;
        }
    }

    printf("버전 %d 복구 완료!\n", version);
    result = 0;

cleanup:
    // 파일 잠금 해제 및 자원 정리
    if (unlock_file(fd) == -1)
        result = -1;

    close(fd);
    close(src);

    return result;
}

