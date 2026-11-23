#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "common.h"
#include "ipc.h"


/*
 * 함수명: print_menu
 * 기능: 사용자에게 파일 관리 메뉴를 출력한다.
 */
void print_menu(void) {
    printf("\n===== FileGuard Client =====\n");
    printf("1. 파일 등록\n");
    printf("2. 파일 내용 조회\n");
    printf("3. 파일 수정\n");
    printf("4. 변경 이력 조회\n");
    printf("5. 이전 버전 복구\n");
    printf("6. 파일 잠금 상태 조회\n");
    printf("7. 종료\n");
    printf("선택 > ");
    fflush(stdout);
}

/*
 * 함수명: read_line
 * 기능: 사용자 입력을 문자열로 읽고 개행 문자를 제거한다.
 *
 * 반환값
 *  0 : 입력 성공
 * -1 : 입력 실패 또는 버퍼 크기 초과
 */
int read_line(char *buffer, size_t size) {
    if (size == 0)
        return -1;

    if (fgets(buffer, size, stdin) == NULL)
        return -1;

    size_t len = strlen(buffer);

    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    } else if (!feof(stdin)) {
        int ch;

        // 입력이 너무 길면 남은 문자를 비우고 오류 반환
        while ((ch = getchar()) != '\n' && ch != EOF)
            ;

        return -1;
    }

    return 0;
}

/*
 * 함수명: create_request
 * 기능: 사용자가 선택한 작업에 맞게 Request 구조체를 생성한다.
 *
 * 반환값
 *  0 : 요청 생성 성공
 * -1 : 잘못된 입력
 */
int create_request(int choice, Request *req) {
    char input[32];

    memset(req, 0, sizeof(*req));

    // 요청을 전송할 클라이언트의 PID 저장
    req->client_pid = getpid();

    switch (choice) {
        case 1:
            req->type = FILE_REGISTER;
            break;
        case 2:
            req->type = FILE_READ;
            break;
        case 3:
            req->type = FILE_MODIFY;
            break;
        case 4:
            req->type = FILE_HISTORY;
            break;
        case 5:
            req->type = FILE_RESTORE;
            break;
        case 6:
            req->type = FILE_LOCK_STATUS;
            break;
        default:
            return -1;
    }

    // 모든 파일 작업에 공통으로 필요한 파일명 입력
    printf("파일 이름 > ");
    fflush(stdout);

    if (read_line(req->filename, sizeof(req->filename)) == -1)
        return -1;

    if (req->filename[0] == '\0') {
        printf("파일 이름을 입력해야 합니다.\n");
        return -1;
    }

    // 파일 수정 요청일 경우 새 내용 입력
    if (req->type == FILE_MODIFY) {
        printf("수정할 내용 > ");
        fflush(stdout);

        if (read_line(req->content, sizeof(req->content)) == -1)
            return -1;
    }

    // 파일 복구 요청일 경우 버전 번호 입력
    if (req->type == FILE_RESTORE) {
        char *end;
        long version;

        printf("복구할 버전 번호 > ");
        fflush(stdout);

        if (read_line(input, sizeof(input)) == -1)
            return -1;

        version = strtol(input, &end, 10);

        if (end == input || *end != '\0' ||
            version <= 0 || version > 2147483647L) {
            printf("올바른 버전 번호를 입력하세요.\n");
            return -1;
        }

        req->version = (int)version;
    }

    return 0;
}

/*
 * 함수명: main
 * 기능: 사용자 메뉴 처리 및 서버와 FIFO 통신
 *
 * 처리 순서
 * 1. 메뉴 선택 및 Request 생성
 * 2. 클라이언트 전용 응답 FIFO 생성
 * 3. 서버에 요청 전송
 * 4. 서버 응답 수신 및 출력
 * 5. FIFO 자원 정리
 */
int main(void) {
    char input[32];
    char fifo_path[256];

    Request req;
    Response res;

    /*
     * 응답 FIFO 경로 생성
     * 클라이언트 PID를 이용해 고유한 경로 생성
     */
    make_client_fifo_path(getpid(),
                          fifo_path, sizeof(fifo_path));

    /*
     * SIGPIPE 무시
     * 서버 연결 종료 시 프로그램이 강제 종료되는 것을 방지
     */
    signal(SIGPIPE, SIG_IGN);

    /*
     * 클라이언트 전용 응답 FIFO 생성
     */
    if (mkfifo(fifo_path, 0600) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo client");
            return 1;
        }

        // 이전 실행에서 남은 FIFO가 있을 수 있으므로
        // 기존 경로는 재사용하지 않고 종료한다.
        fprintf(stderr, "응답 FIFO 경로가 이미 존재합니다.\n");
        return 1;
    }

    /*
     * FIFO를 읽기/쓰기 모드로 연다.
     *
     * 서버가 아직 응답 FIFO를 열지 않았더라도
     * 클라이언트가 먼저 준비할 수 있도록 한다.
     */
    int response_fd = open(fifo_path, O_RDWR);

    if (response_fd == -1) {
        perror("open client FIFO");
        unlink(fifo_path);
        return 1;
    }

    /*
     * 사용자 메뉴 처리 반복문
     */
    while (1) {
        print_menu();

        if (read_line(input, sizeof(input)) == -1)
            break;

        char *end;
        long choice = strtol(input, &end, 10);

        /*
         * 잘못된 메뉴 번호 검사
         */
        if (end == input || *end != '\0' ||
            choice < 1 || choice > 7) {
            printf("잘못된 메뉴 선택입니다.\n");
            continue;
        }

        /*
         * 종료 메뉴
         */
        if (choice == 7) {
            printf("클라이언트를 종료합니다.\n");
            break;
        }

        /*
         * 사용자의 입력으로 Request 생성
         */
        if (create_request((int)choice, &req) == -1) {
            printf("요청 생성 실패\n");
            continue;
        }

        /*
         * 서버 FIFO로 요청 전송
         */
        if (send_request(&req) == -1) {
            printf("서버에 요청을 전송하지 못했습니다.\n");
            continue;
        }

        printf("\n[서버로 요청 전송 완료]\n");

        /*
         * 클라이언트 전용 FIFO에서 응답 수신
         */
        if (receive_response(response_fd, &res) == -1) {
            printf("서버 응답 수신 실패\n");
            break;
        }

        /*
         * 서버에서 받은 처리 결과 출력
         */
        printf("\n[서버 응답]\n");
        printf("상태 코드: %d\n", res.status);
        printf("메시지: %.*s\n",
               (int)sizeof(res.message), res.message);
    }

    /*
     * 프로그램 종료 시 FIFO 정리
     */
    close(response_fd);
    unlink(fifo_path);

    return 0;
}

