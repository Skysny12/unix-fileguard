
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "log.h"

/*
 * 파일 수정 및 복구 작업의 내역을 로그 파일에 기록한다.
 * 작업 시간, 프로세스 ID, 작업 종류, 파일명 등을 저장하여
 * 어떤 프로세스가 언제 파일을 변경했는지 확인할 수 있도록 한다.
 */
int log_action(const char *action,
               const char *filename,
               int version) {

    // 작업 종류와 파일명이 전달되었는지 확인
    if (action == NULL || filename == NULL)
        return -1;

    // 현재 시간을 가져와 지역 시간으로 변환
    time_t now = time(NULL);
    struct tm tm_info;

    if (localtime_r(&now, &tm_info) == NULL)
        return -1;

    // 로그에 표시할 시간을 문자열 형태로 변환
    char timestamp[32];

    if (strftime(timestamp, sizeof(timestamp),
                 "%Y-%m-%d %H:%M:%S", &tm_info) == 0)
        return -1;

    char message[768];
    int len;

    // 복구 작업이면 버전 번호를 포함하여 메시지 생성
    if (version > 0) {
        len = snprintf(message, sizeof(message),
            "[%s] PID %ld | %s | %s | VERSION %d\n",
            timestamp, (long)getpid(),
            action, filename, version);
    } else {
        // 일반 파일 수정은 버전 번호 없이 기록
        len = snprintf(message, sizeof(message),
            "[%s] PID %ld | %s | %s\n",
            timestamp, (long)getpid(),
            action, filename);
    }

    // 메시지가 버퍼 크기를 초과하지 않았는지 확인
    if (len < 0 || (size_t)len >= sizeof(message))
        return -1;

    // 로그 파일을 추가 쓰기 모드로 열기
    // O_APPEND를 사용하여 기존 로그를 유지하고 끝에 기록
    int fd = open("fileguard.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd == -1) {
        perror("로그 파일 열기 실패");
        return -1;
    }

    // 로그 메시지를 파일에 기록
    // EINTR 발생 시 중단된 쓰기 작업을 다시 시도
    ssize_t written;

    do {
        written = write(fd, message, (size_t)len);
    } while (written == -1 && errno == EINTR);

    // 요청한 길이만큼 기록되지 않았다면 오류 처리
    if (written != len) {
        fprintf(stderr, "로그 기록 실패\n");
        close(fd);
        return -1;
    }

    // 로그 기록이 끝나면 파일 디스크립터 반환
    if (close(fd) == -1) {
        perror("로그 파일 닫기 실패");
        return -1;
    }

    return 0;
}

