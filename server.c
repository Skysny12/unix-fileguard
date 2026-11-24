
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "common.h"
#include "ipc.h"
#include "file_manager.h"
/*
 * UNIX 기반 파일 충돌 방지 및 변경 이력 관리 시스템
 *
 * 파일명: server.c
 * 기능: FIFO 통신 및 다중 프로세스 기반 요청 처리
 *
 * 주요 기능
 * 1. 서버 FIFO 생성 및 클라이언트 요청 수신
 * 2. fork()를 활용한 자식 프로세스 생성
 * 3. 자식 프로세스에서 요청 처리 및 응답 전송
 * 4. waitpid()를 활용한 종료된 자식 프로세스 회수
 * 5. SIGINT 처리 및 서버 정상 종료
 *
 * 실제 파일 수정 및 복구 기능은 추후 연결한다.
 */

// 서버 실행 상태
static volatile sig_atomic_t running = 1;

/*
 * 함수명: handle_signal
 * 기능: Ctrl+C(SIGINT) 발생 시 서버 종료 요청
 *
 * 시그널 핸들러에서는 실행 상태만 변경하고
 * 실제 자원 정리는 main()에서 수행한다.
 */
static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

/*
 * 함수명: handle_sigchld
 * 기능: 종료된 자식 프로세스를 회수한다.
 *
 * fork()로 생성된 자식이 종료되면 SIGCHLD가 발생한다.
 * waitpid()에 WNOHANG 옵션을 사용하여
 * 부모 프로세스가 대기하지 않고 종료된 자식을 회수한다.
 *
 * 종료된 자식을 회수하지 않으면
 * 좀비 프로세스가 발생할 수 있다.
 */
static void handle_sigchld(int sig) {
    int saved_errno = errno;
    (void)sig;

    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // 종료된 자식 프로세스를 모두 회수
    }

    errno = saved_errno;
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
 * 기능: 클라이언트 요청 종류에 따라 파일 관리 함수 실행
 *
 * 현재 구현 범위
 * 1. 파일 등록
 * 2. 파일 내용 조회
 *
 * 파일 수정, 변경 이력 및 복구 기능은 추후 연동한다.
 */
static void process_request(const Request *req, Response *res) {
    memset(res, 0, sizeof(*res));
    res->status = -1;

    // 파일명이 비어 있거나 문자열이 종료되지 않은 경우
    if (memchr(req->filename, '\0',
               sizeof(req->filename)) == NULL ||
        req->filename[0] == '\0') {
        snprintf(res->message, sizeof(res->message),
                 "올바르지 않은 파일 이름입니다.");
        return;
    }

    switch (req->type) {

        case FILE_REGISTER:
            // 팀원 B의 파일 등록 함수 호출
            if (register_file(req->filename) == 0) {
                res->status = 0;
                snprintf(res->message, sizeof(res->message),
                         "파일 등록 성공: %s", req->filename);
            } else {
                snprintf(res->message, sizeof(res->message),
                         "파일 등록 실패");
            }
            break;

        case FILE_READ: {
            // 팀원 B의 파일 조회 함수 호출
            int n = read_file(req->filename,
                              res->message,
                              sizeof(res->message));

            if (n >= 0) {
                res->status = 0;
            } else {
                snprintf(res->message, sizeof(res->message),
                         "파일 조회 실패");
            }
            break;
        }

        default:
            snprintf(res->message, sizeof(res->message),
                     "아직 구현되지 않은 기능입니다.");
            break;
    }
}


int main(void) {
    int fd;
    struct sigaction sa = {0};
    struct sigaction sa_chld = {0};

    /*
     * 1. SIGINT 신호 처리 설정
     *
     * Ctrl+C 입력 시 handle_signal()을 실행하여
     * 서버의 메인 반복문을 종료한다.
     */
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        return 1;
    }

    /*
     * 2. SIGCHLD 신호 처리 설정
     *
     * 자식 프로세스 종료 시 handle_sigchld()를 실행한다.
     *
     * SA_RESTART:
     * SIGCHLD 발생으로 중단된 read() 등의 시스템 콜을
     * 가능한 경우 자동으로 다시 실행하도록 한다.
     */
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction SIGCHLD");
        return 1;
    }

    /*
     * SIGPIPE 신호 무시
     *
     * 클라이언트가 응답 FIFO를 닫은 경우에도
     * 서버가 강제 종료되지 않도록 한다.
     */
    signal(SIGPIPE, SIG_IGN);

    /*
     * 3. 서버 FIFO 생성
     *
     * 모든 클라이언트가 요청을 전송하는
     * 공용 FIFO 통신 경로를 생성한다.
     */
    if (create_server_fifo() == -1)
        return 1;

    /*
     * 4. 서버 FIFO 열기
     *
     * Linux에서 O_RDWR로 FIFO를 열면
     * 상대 프로세스 연결 없이도 열 수 있다.
     */
    fd = open(SERVER_FIFO, O_RDWR);

    if (fd == -1) {
        perror("open server FIFO");
        return 1;
    }

    /*
     * 5. 서버 실행 정보 출력
     */
    printf("===== FileGuard Server =====\n");
    printf("Server PID: %ld\n", (long)getpid());
    printf("FIFO: %s\n", SERVER_FIFO);
    printf("Waiting for requests...\n");
    fflush(stdout);

    /*
     * 6. 클라이언트 요청 수신 반복문
     *
     * 부모 프로세스는 FIFO에서 요청을 수신한다.
     * 요청이 도착하면 fork()로 자식 프로세스를 생성한다.
     *
     * 각 자식은 독립적으로 요청을 처리하므로
     * 부모는 다음 요청을 계속 받을 수 있다.
     */
    while (running) {
        Request req;

        int result = read_request(fd, &req);

        if (result == -1) {
            if (!running)
                break;
            continue;
        }

        if (result == 0)
            continue;

        /*
         * 클라이언트 요청 정보 출력
         */
        printf("\n[요청 수신]\n");
        printf("Client PID: %ld\n",
               (long)req.client_pid);
        printf("Request type: %d\n", req.type);
        printf("Filename: %.*s\n",
               (int)sizeof(req.filename), req.filename);
        fflush(stdout);

        /*
         * 7. fork()를 이용한 자식 프로세스 생성
         *
         * 반환값
         *  0보다 큰 값 : 부모 프로세스 (자식 PID)
         *  0          : 자식 프로세스
         * -1          : 프로세스 생성 실패
         */
        pid_t child_pid = fork();

        if (child_pid == -1) {
            perror("fork");

            /*
             * 자식 생성에 실패한 경우
             * 부모가 클라이언트에 오류 응답을 보낸다.
             */
            Response error_res;
            memset(&error_res, 0, sizeof(error_res));

            error_res.status = -1;
            snprintf(error_res.message,
                     sizeof(error_res.message),
                     "서버에서 자식 프로세스 생성 실패");

            send_response(req.client_pid, &error_res);
            continue;
        }

        if (child_pid == 0) {
            /*
             * 8. 자식 프로세스
             *
             * 부모가 사용하는 요청 FIFO 디스크립터를
             * 닫고 현재 요청만 독립적으로 처리한다.
             */
            close(fd);

            /*
             * 자식은 Ctrl+C로 부모와 함께 종료되는
             * 것을 피하도록 SIGINT를 무시한다.
             */
            signal(SIGINT, SIG_IGN);

            printf("[자식 %ld] 요청 처리 시작\n",
                   (long)getpid());
            fflush(stdout);

            /*
             * 요청 처리 및 서버 응답 생성
             */
            Response res;
            process_request(&req, &res);

            /*
             * 처리 결과를 클라이언트 전용 FIFO로 전송
             */
            if (send_response(req.client_pid, &res) == -1) {
                fprintf(stderr,
                        "[자식 %ld] 응답 전송 실패\n",
                        (long)getpid());
                _exit(1);
            }

            printf("[자식 %ld] 응답 전송 완료\n",
                   (long)getpid());
            fflush(stdout);

            /*
             * 자식 프로세스 종료
             *
             * _exit()는 fork() 이후 자식이
             * 즉시 종료할 때 사용한다.
             */
            _exit(0);
        }

        /*
         * 9. 부모 프로세스
         *
         * 부모는 자식의 종료를 기다리지 않고
         * 새로운 클라이언트 요청을 계속 수신한다.
         *
         * 종료된 자식은 SIGCHLD 핸들러에서
         * waitpid()로 회수한다.
         */
        printf("[부모 %ld] 자식 프로세스 생성: %ld\n",
               (long)getpid(), (long)child_pid);
        fflush(stdout);
    }

    /*
     * 10. 서버 종료 및 자원 정리
     *
     * close(): FIFO 파일 디스크립터 닫기
     * unlink(): 서버 FIFO 경로 삭제
     *
     * 서버 종료 후 새 클라이언트의 요청이
     * 들어오지 않도록 통신 경로를 제거한다.
     */
    close(fd);
    unlink(SERVER_FIFO);

    printf("\nServer terminated.\n");

    return 0;
}

