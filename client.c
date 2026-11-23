
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common.h"

/*
 * UNIX 기반 파일 충돌 방지 및 변경 이력 관리 시스템
 *
 * 파일명: client.c
 * 기능: 클라이언트 사용자 메뉴 및 요청 데이터 생성
 *
 * 주요 기능
 * 1. 사용자에게 파일 관리 메뉴 제공
 * 2. 선택한 작업에 필요한 정보 입력
 * 3. Request 구조체에 요청 데이터 저장
 * 4. 잘못된 메뉴 입력 처리
 *
 * 현재는 요청 데이터를 화면에 출력하며,
 * FIFO를 통한 서버 통신은 추후 구현한다.
 */

// 사용자 메뉴 출력
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
}

/*
 * 함수명: read_line
 * 기능: 사용자 입력을 안전하게 문자열로 읽는다.
 * 반환값: 성공 시 0, 실패 시 -1
 */
int read_line(char *buffer, size_t size) {
    if (fgets(buffer, size, stdin) == NULL)
        return -1;

    size_t len = strlen(buffer);

    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    } else {
        // 입력 버퍼에 남은 문자 제거
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
            ;
    }

    return 0;
}

/*
 * 함수명: create_request
 * 기능: 사용자 선택에 따라 서버 요청 구조체를 생성한다.
 * 반환값: 성공 시 0, 실패 시 -1
 */
int create_request(int choice, Request *req) {
    char input[32];

    memset(req, 0, sizeof(*req));

    // 요청을 보낸 클라이언트의 PID 저장
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

    printf("파일 이름 > ");

    if (read_line(req->filename, sizeof(req->filename)) == -1)
        return -1;

    if (req->filename[0] == '\0') {
        printf("파일 이름을 입력해야 합니다.\n");
        return -1;
    }

    // 파일 수정 시 새 내용 입력
    if (req->type == FILE_MODIFY) {
        printf("수정할 내용 > ");

        if (read_line(req->content, sizeof(req->content)) == -1)
            return -1;
    }

    // 파일 복구 시 버전 번호 입력
    if (req->type == FILE_RESTORE) {
        printf("복구할 버전 번호 > ");

        if (read_line(input, sizeof(input)) == -1)
            return -1;

        char *end;
        long version = strtol(input, &end, 10);

        if (end == input || *end != '\0' ||
            version <= 0 || version > 2147483647L) {
            printf("올바른 버전 번호를 입력하세요.\n");
            return -1;
        }

        req->version = (int)version;
    }

    return 0;
}

int main(void) {
    char input[32];
    Request req;

    while (1) {
        print_menu();

        if (read_line(input, sizeof(input)) == -1)
            break;

        // 메뉴 번호 변환
        char *end;
        long choice = strtol(input, &end, 10);

        if (end == input || *end != '\0' ||
            choice < 1 || choice > 7) {
            printf("잘못된 메뉴 선택입니다.\n");
            continue;
        }

        if (choice == 7) {
            printf("클라이언트를 종료합니다.\n");
            break;
        }

        if (create_request((int)choice, &req) == -1) {
            printf("요청 생성에 실패했습니다.\n");
            continue;
        }

        /*
         * 현재는 IPC 연결 전이므로 요청 정보만 출력한다.
         * 추후 FIFO write() 호출로 교체할 예정이다.
         */
        printf("\n[요청 생성 완료]\n");
        printf("요청 종류: %d\n", req.type);
        printf("클라이언트 PID: %ld\n", (long)req.client_pid);
        printf("파일 이름: %s\n", req.filename);

        if (req.type == FILE_MODIFY)
            printf("수정 내용: %s\n", req.content);

        if (req.type == FILE_RESTORE)
            printf("복구 버전: %d\n", req.version);
    }

    return 0;
}

