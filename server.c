
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "common.h"
#include "ipc.h"
#include "file_manager.h"
#include "history.h"

/* 서버 실행 여부: SIGINT 수신 시 0으로 변경 */
static volatile sig_atomic_t running = 1;

/* 종료 신호를 받으면 반복문이 끝나도록 상태를 변경한다. */
static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

/* 종료된 자식 프로세스를 회수해 좀비 프로세스를 방지한다. */
static void handle_sigchld(int sig) {
    int saved_errno = errno;
    (void)sig;

    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // 종료된 자식이 남아 있는 동안 반복한다.
    }

    errno = saved_errno;
}

/* FIFO에서 요청 구조체를 끝까지 읽는다. (1: 성공, 0: 종료, -1: 오류) */
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

/* 백업 파일 목록을 클라이언트 응답 메시지로 작성한다. */
static int get_history_message(const char *filename,
                               char *message, size_t size) {
    char path[512];
    size_t used = 0;
    int count = 0;

    if (filename == NULL || message == NULL || size == 0)
        return -1;

    message[0] = '\0';

    // v1부터 존재하는 백업 파일을 찾아 버전과 크기를 기록한다.
    for (int version = 1; version <= 9999; version++) {
        int len = snprintf(path, sizeof(path),
                           "data/history/%s.v%d", filename, version);

        if (len < 0 || (size_t)len >= sizeof(path))
            return -1;

        struct stat info;
        if (stat(path, &info) == -1) {
            if (errno == ENOENT)
                continue;
            return -1;
        }

        if (!S_ISREG(info.st_mode))
            continue;

        int n = snprintf(message + used, size - used,
                         "버전 %d: %lld bytes\n",
                         version, (long long)info.st_size);

        if (n < 0)
            return -1;

        // 응답 버퍼가 부족하면 잘라서 전송한다.
        if ((size_t)n >= size - used) {
            snprintf(message + used, size - used,
                     "...이하 생략");
            return 0;
        }

        used += (size_t)n;
        count++;
    }

    if (count == 0)
        snprintf(message, size, "저장된 변경 이력이 없습니다.");

    return 0;
}

/* 요청 종류에 맞는 파일 관리 함수를 호출하고 결과를 저장한다. */
static void process_request(const Request *req, Response *res) {
    memset(res, 0, sizeof(*res));
    res->status = -1;

    // 파일명은 반드시 종료 문자를 포함하고 단순 파일명이어야 한다.
    if (memchr(req->filename, '\0',
               sizeof(req->filename)) == NULL ||
        req->filename[0] == '\0' ||
        strchr(req->filename, '/') != NULL) {

        snprintf(res->message, sizeof(res->message),
                 "올바르지 않은 파일 이름입니다.");
        return;
    }

    // 등록·조회·수정·이력·복구·잠금 조회 요청을 구분한다.
    switch (req->type) {

        case FILE_REGISTER:
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

        case FILE_MODIFY:

            if (memchr(req->content, '\0',
                       sizeof(req->content)) == NULL) {
                snprintf(res->message, sizeof(res->message),
                         "수정할 내용이 너무 깁니다.");
                break;
            }

            // 내부에서 잠금·백업·수정·로그 기록까지 처리한다.
            if (update_file(req->filename, req->content) == 0) {
                res->status = 0;
                snprintf(res->message, sizeof(res->message),
                         "파일 수정 완료: %s", req->filename);
            } else {
                snprintf(res->message, sizeof(res->message),
                         "파일 수정 실패");
            }
            break;

        case FILE_HISTORY:
            if (get_history_message(req->filename,
                                    res->message,
                                    sizeof(res->message)) == 0) {
                res->status = 0;
            } else {
                snprintf(res->message, sizeof(res->message),
                         "변경 이력 조회 실패");
            }
            break;

        case FILE_RESTORE:
            if (restore_file(req->filename, req->version) == 0) {
                res->status = 0;
                snprintf(res->message, sizeof(res->message),
                         "버전 %d 복구 완료",
                         req->version);
            } else {
                snprintf(res->message, sizeof(res->message),
                         "이전 버전 복구 실패");
            }
            break;

        case FILE_LOCK_STATUS: {
            int fd = open(req->filename, O_RDWR);

            if (fd == -1) {
                snprintf(res->message, sizeof(res->message),
                         "파일 열기 실패");
                break;
            }

            struct flock lock = {0};

            lock.l_type = F_WRLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = 0;
            lock.l_len = 0;

            // 다른 프로세스가 보유한 충돌 잠금을 확인한다.
            if (fcntl(fd, F_GETLK, &lock) == -1) {
                snprintf(res->message, sizeof(res->message),
                         "잠금 상태 조회 실패");
            } else {
                res->status = 0;

                if (lock.l_type == F_UNLCK) {
                    snprintf(res->message, sizeof(res->message),
                             "현재 충돌하는 파일 잠금이 없습니다.");
                } else {
                    snprintf(res->message, sizeof(res->message),
                             "파일 잠금 중 (PID: %ld)",
                             (long)lock.l_pid);
                }
            }

            close(fd);
            break;
        }

        default:
            snprintf(res->message, sizeof(res->message),
                     "지원하지 않는 요청입니다.");
            break;
    }
}

/* 서버 FIFO를 열고, 요청마다 자식 프로세스를 만들어 처리한다. */
int main(void) {
    int fd;
    struct sigaction sa = {0};
    struct sigaction sa_chld = {0};

    // Ctrl+C를 받으면 안전하게 서버를 종료한다.
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        return 1;
    }

    // 자식 종료 시 waitpid()로 회수하고 중단된 read()는 가능하면 재시작한다.
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction SIGCHLD");
        return 1;
    }

    // 클라이언트가 응답 FIFO를 닫아도 서버가 종료되지 않게 한다.
    signal(SIGPIPE, SIG_IGN);

    // 클라이언트가 요청을 보낼 공용 FIFO를 생성한다.
    if (create_server_fifo() == -1)
        return 1;

    // 읽기·쓰기 모드로 열어 연결 상대가 없어도 대기할 수 있게 한다.
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

    // 부모는 요청을 계속 받고, 각 요청은 자식이 독립적으로 처리한다.
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

        printf("\n[요청 수신]\n");
        printf("Client PID: %ld\n",
               (long)req.client_pid);
        printf("Request type: %d\n", req.type);
        printf("Filename: %.*s\n",
               (int)sizeof(req.filename), req.filename);
        fflush(stdout);

        // fork() 실패 시 부모가 오류 응답을 보낸다.
        pid_t child_pid = fork();

        if (child_pid == -1) {
            perror("fork");

            Response error_res;
            memset(&error_res, 0, sizeof(error_res));

            error_res.status = -1;
            snprintf(error_res.message,
                     sizeof(error_res.message),
                     "서버에서 자식 프로세스 생성 실패");

            send_response(req.client_pid, &error_res);
            continue;
        }

        // 자식은 전달받은 요청 하나를 처리하고 클라이언트에 응답한다.
        if (child_pid == 0) {

            close(fd);

            signal(SIGINT, SIG_IGN);

            printf("[자식 %ld] 요청 처리 시작\n",
                   (long)getpid());
            fflush(stdout);

            Response res;
            process_request(&req, &res);

            if (send_response(req.client_pid, &res) == -1) {
                fprintf(stderr,
                        "[자식 %ld] 응답 전송 실패\n",
                        (long)getpid());
                _exit(1);
            }

            printf("[자식 %ld] 응답 전송 완료\n",
                   (long)getpid());
            fflush(stdout);

            _exit(0);
        }

        printf("[부모 %ld] 자식 프로세스 생성: %ld\n",
               (long)getpid(), (long)child_pid);
        fflush(stdout);
    }

    // 서버 종료 시 공용 FIFO를 닫고 경로를 삭제한다.
    close(fd);
    unlink(SERVER_FIFO);

    printf("\nServer terminated.\n");
    
    return 0;
}
