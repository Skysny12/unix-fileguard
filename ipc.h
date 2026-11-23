
#ifndef IPC_H
#define IPC_H
#include <stddef.h>
#include "common.h"

/*
 * 파일명: ipc.h
 * 기능: 서버와 클라이언트 간 FIFO 통신 함수 선언
 */

// 서버가 요청을 수신하는 공용 FIFO 경로
#define SERVER_FIFO "/tmp/fileguard_server_fifo"

// 클라이언트별 응답 FIFO 경로 생성
void make_client_fifo_path(pid_t pid,
                           char *path,
                           size_t size);

// 서버 FIFO 생성
int create_server_fifo(void);

// 서버에 요청 전송
int send_request(const Request *req);

// 서버 응답 수신
int receive_response(int fd, Response *res);

// 클라이언트에 응답 전송
int send_response(pid_t client_pid,
                  const Response *res);

#endif

