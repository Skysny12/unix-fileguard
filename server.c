
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include "common.h"
#include "ipc.h"

/*
 * UNIX 기반 파일 충돌 방지 및 변경 이력 관리 시스템
 *
 * 파일명: server.c
 * 기능: FIFO를 통한 클라이언트 요청 수신 및 응답 전송
 *
 * 주요 기능
 * 1. 서버 FIFO 생성 및 실행
 * 2. 클라이언트 Request 수신
 * 3. 요청 정보 확인 및 Response 생성
 * 4. 클라이언트 전용 FIFO로 응답 전송
 * 5. SIGINT 처리 및 서버 정상 종료
 *
 * 실제 파일 수정 및 복구 기능은 추후 연결한다.
 */

// 서버 실행 상태
static volatile sig_atomic_t running = 1;

/*
 * 함수명: handle_signal
 * 기능: Ctrl+C(SIGINT) 발생 시 서버 종료 요청
 */
static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

/*
 * 함수명: read_request
 * 기능: FIFO에서 Request 구조체 하나를 완전히 읽는다.
 *
 * 반환값
 *  1 : 요청 수신 성공
 *  0 : FIFO 읽기 종료
 * -1 : 오류 또는 종료 신호
 *
 * read()는 요청한 바이트 수보다 적게 읽을 수 있으므로
 * 반복문을 통해 구조체 전체를 수신한다.
 */
static int read_request(int fd, Request *req) {
    size_t received = 0;
    char *buffer = (char *)req;

    while (received < sizeof(Request)) {
        ssize_t n = read(fd,
                         buffer + received,
                         sizeof(Request) - received);

        if (n == -1) {
            if (errno == EINTR) {
                if (!running)
                    return -1;
                continue;
            }

            perror("read request");
            return -1;
        }

        if (n == 0)
            return 0;

        received += (size_t)n;
    }

    return 1;
}

/*
 * 함수명: process_request
 * 기능: 클라이언트 요청을 확인하고 응답 생성
 *
 * 현재는 IPC 동작 확인을 위한 임시 응답만 생성한다.
 * 추후 팀원 B가 구현한 파일 관리 함수를 연결한다.
 */
static void process_request(const Request *req, Response *res) {
    memset(res, 0, sizeof(*res));

    res->status = 0;

    snprintf(res->message, sizeof(res->message),
             "서버가 요청을 정상적으로 받았습니다! (작업: %d)",
             req->type);
}

int main(void) {
    int fd;
    struct sigaction sa = {0};

    /*
     * 1. 시그널 처리 설정
     *
     * SIGINT: Ctrl+C를 통한 정상 종료
     * SIGPIPE: 응답 FIFO 연결이 끊겼을 때
     *          서버가 강제 종료되지 않도록 무시
     */
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    /*
     * 2. 서버 FIFO 생성
     *
     * 클라이언트가 요청을 보내는 공용 FIFO를 생성한다.
     */
    if (create_server_fifo() == -1)
        return 1;

    /*
     * 3. FIFO 열기
     *
     * Linux에서 O_RDWR로 FIFO를 열면
     * 상대 프로세스 연결 없이도 열 수 있다.
     */
    fd = open(SERVER_FIFO, O_RDWR);

    if (fd == -1) {
        perror("open server FIFO");
        return 1;
    }

    printf("===== FileGuard Server =====\n");
    printf("Server PID: %ld\n", (long)getpid());
    printf("FIFO: %s\n", SERVER_FIFO);
    printf("Waiting for requests...\n");
    fflush(stdout);

    /*
     * 4. 클라이언트 요청 수신 반복문
     *
     * FIFO를 통해 Request를 읽고
     * 클라이언트에게 처리 결과를 전송한다.
     */
    while (running) {
        Request req;
        Response res;

        int result = read_request(fd, &req);

        if (result == -1) {
            if (!running)
                break;
            continue;
        }

        if (result == 0)
            continue;

        /*
         * 클라이언트로부터 받은 정보 출력
         */
        printf("\n[요청 수신]\n");
        printf("Client PID: %ld\n",
               (long)req.client_pid);
        printf("Request type: %d\n", req.type);
        printf("Filename: %.*s\n",
               (int)sizeof(req.filename), req.filename);
        fflush(stdout);

        /*
         * 요청 처리 후 Response 생성
         */
        process_request(&req, &res);

        /*
         * 클라이언트 전용 FIFO에 응답 전송
         */
        if (send_response(req.client_pid, &res) == -1) {
            fprintf(stderr, "응답 전송 실패\n");
        } else {
            printf("[응답 전송 완료]\n");
        }
    }

    /*
     * 5. 서버 종료 및 자원 정리
     */
    close(fd);
    unlink(SERVER_FIFO);

    printf("\nServer terminated.\n");

    return 0;
}

