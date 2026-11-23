#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>

#define MAX_PATH 256
#define MAX_CONTENT 1024

// 작업 종류
typedef enum {
    FILE_REGISTER = 1,
    FILE_READ,
    FILE_MODIFY,
    FILE_HISTORY,
    FILE_RESTORE,
    FILE_LOCK_STATUS
} RequestType;

// 클라이언트가 서버에 보내는 요청
typedef struct {
    RequestType type;
    pid_t client_pid;
    char filename[MAX_PATH];
    char content[MAX_CONTENT];
    int version;
} Request;

// 서버가 클라이언트에 보내는 응답
typedef struct {
    int status;  // 0: 성공, -1: 실패
    char message[MAX_CONTENT];
} Response;

#endif
