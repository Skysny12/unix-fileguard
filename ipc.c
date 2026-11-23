
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "ipc.h"

/*
 * UNIX 기반 파일 충돌 방지 및 변경 이력 관리 시스템
 *
 * 파일명: ipc.c
 * 기능: FIFO를 이용한 서버-클라이언트 간 통신
 *
 * 주요 기능
 * 1. 서버 FIFO 생성
 * 2. 클라이언트별 응답 FIFO 경로 생성
 * 3. 클라이언트 요청 전송
 * 4. 서버 응답 수신
 * 5. 서버 응답 전송
 */

/*
 * 함수명: make_client_fifo_path
 * 기능: 클라이언트 PID를 이용하여 응답 FIFO 경로 생성
 *
 * 매개변수
 * pid  : 클라이언트 프로세스 ID
 * path : 생성된 경로를 저장할 문자열 배열
 * size : 배열 크기
 *
 * 클라이언트마다 서로 다른 FIFO 경로를 사용하여
 * 응답 메시지가 섞이지 않도록 한다.
 */
void make_client_fifo_path(pid_t pid,
                           char *path,
                           size_t size) {
    snprintf(path, size,
             "/tmp/fileguard_client_%ld",
             (long)pid);
}

/*
 * 함수명: create_server_fifo
 * 기능: 서버 요청을 수신할 FIFO 생성
 *
 * 반환값
 *  0 : 성공
 * -1 : 실패
 *
 * mkfifo()를 이용해 Named Pipe를 생성한다.
 * 이미 생성되어 있다면 실제 FIFO인지 확인한다.
 */
int create_server_fifo(void) {
    struct stat st;

    if (mkfifo(SERVER_FIFO, 0600) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo");
            return -1;
        }

        // 기존 경로가 FIFO인지 확인
        if (lstat(SERVER_FIFO, &st) == -1 ||
            !S_ISFIFO(st.st_mode)) {
            fprintf(stderr,
                    "Server FIFO path is invalid.\n");
            return -1;
        }
    }

    return 0;
}

/*
 * 함수명: send_request
 * 기능: 클라이언트 요청을 서버에 전송
 *
 * 매개변수
 * req : 서버에 보낼 Request 구조체
 *
 * 반환값
 *  0 : 전송 성공
 * -1 : 전송 실패
 *
 * open()으로 서버 FIFO를 열고
 * write()로 요청 데이터를 전송한다.
 */
int send_request(const Request *req) {
    int fd;
    ssize_t n;

    if (req == NULL)
        return -1;

    // 서버 FIFO를 쓰기 전용으로 열기
    // 서버가 없을 때 무한 대기하지 않도록 설정
    fd = open(SERVER_FIFO, O_WRONLY | O_NONBLOCK);

    if (fd == -1) {
        perror("open server FIFO");
        return -1;
    }

    /*
     * 요청 메시지가 PIPE_BUF 이하여야
     * 여러 클라이언트가 동시에 전송해도
     * 메시지 내용이 서로 섞이지 않는다.
     */
    long pipe_buf = fpathconf(fd, _PC_PIPE_BUF);

    if (pipe_buf == -1 ||
        sizeof(Request) > (size_t)pipe_buf) {
        fprintf(stderr,
                "Request exceeds PIPE_BUF.\n");
        close(fd);
        return -1;
    }

    // Request 구조체를 한 번에 전송
    do {
        n = write(fd, req, sizeof(Request));
    } while (n == -1 && errno == EINTR);

    if (n != (ssize_t)sizeof(Request)) {
        if (n == -1)
            perror("write request");
        else
            fprintf(stderr, "Incomplete request.\n");

        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/*
 * 함수명: receive_response
 * 기능: 서버에서 보낸 응답을 클라이언트가 수신
 *
 * 매개변수
 * fd  : 클라이언트 응답 FIFO 디스크립터
 * res : 응답 데이터를 저장할 구조체
 *
 * 반환값
 *  0 : 수신 성공
 * -1 : 수신 실패
 *
 * read()는 요청한 크기보다 적은 데이터를
 * 반환할 수 있으므로 반복하여 수신한다.
 *
 * fd는 블로킹 읽기 모드로 열려 있다고 가정한다.
 */
int receive_response(int fd, Response *res) {
    size_t received = 0;
    char *buffer = (char *)res;

    if (res == NULL)
        return -1;

    while (received < sizeof(Response)) {
        ssize_t n = read(fd,
                         buffer + received,
                         sizeof(Response) - received);

        if (n == -1) {
            if (errno == EINTR)
                continue;

            perror("read response");
            return -1;
        }

        if (n == 0) {
            fprintf(stderr,
                    "Response FIFO closed early.\n");
            return -1;
        }

        received += (size_t)n;
    }

    return 0;
}

/*
 * 함수명: send_response
 * 기능: 서버의 처리 결과를 클라이언트에 전달
 *
 * 매개변수
 * client_pid : 응답을 받을 클라이언트 PID
 * res        : 서버 처리 결과
 *
 * 반환값
 *  0 : 성공
 * -1 : 실패
 *
 * 클라이언트 PID를 이용해 전용 FIFO를 찾고
 * write()로 Response 구조체를 전달한다.
 */
int send_response(pid_t client_pid,
                  const Response *res) {
    char path[256];
    int fd;
    size_t sent = 0;
    const char *buffer = (const char *)res;

    if (res == NULL || client_pid <= 0)
        return -1;

    make_client_fifo_path(client_pid,
                          path, sizeof(path));

    // 클라이언트 응답 FIFO 열기
    fd = open(path, O_WRONLY | O_NONBLOCK);

    if (fd == -1) {
        perror("open client FIFO");
        return -1;
    }

    // 모든 응답 데이터 전송
    while (sent < sizeof(Response)) {
        ssize_t n = write(fd,
                          buffer + sent,
                          sizeof(Response) - sent);

        if (n == -1) {
            if (errno == EINTR)
                continue;

            perror("write response");
            close(fd);
            return -1;
        }

        if (n == 0) {
            close(fd);
            return -1;
        }

        sent += (size_t)n;
    }

    close(fd);
    return 0;
}

