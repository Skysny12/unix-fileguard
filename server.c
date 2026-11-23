
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/*
 * UNIX 기반 파일 충돌 방지 및 변경 이력 관리 시스템
 *
 * 파일명: server.c
 * 기능: 서버 초기화 및 실행 관리
 *
 * 주요 기능
 * 1. 서버 프로세스 실행 및 PID 출력
 * 2. FIFO(Named Pipe)를 생성하여 IPC 통신 환경 준비
 * 3. SIGINT 신호를 처리하여 서버 정상 종료
 * 4. 종료 시 FIFO 및 파일 디스크립터 정리
 *
 * 현재는 서버의 기본 구조를 구현한 단계이며,
 * 클라이언트 요청 수신 및 처리는 추후 추가할 예정이다.
 */

// 서버와 클라이언트가 공통으로 사용할 FIFO 경로
#define SERVER_FIFO "/tmp/fileguard_server_fifo"

// 서버 실행 여부를 나타내는 전역 변수
// 시그널 핸들러에서도 접근하므로 sig_atomic_t 사용
volatile sig_atomic_t running = 1;

/*
 * 함수명: handle_signal
 * 기능: SIGINT(Ctrl+C) 신호 처리
 *
 * 매개변수
 * sig: 전달받은 시그널 번호
 *
 * 시그널 발생 시 실행 상태를 0으로 변경하여
 * 메인 반복문이 정상적으로 종료되도록 한다.
 */
void handle_signal(int sig) {
    (void)sig;  // 사용하지 않는 매개변수 경고 방지
    running = 0;
}

int main(void) {
    int fd;  // 서버 FIFO의 파일 디스크립터

    /*
     * 1. SIGINT 신호 처리 설정
     *
     * sigaction()을 사용하여 Ctrl+C 입력 시
     * 기본 강제 종료 대신 handle_signal()이 실행되도록 한다.
     */
    struct sigaction sa = {0};

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    /*
     * 2. 서버 FIFO 생성
     *
     * mkfifo(): 프로세스 간 통신에 사용할 Named Pipe 생성
     * 0600: 파일 소유자에게 읽기/쓰기 권한 부여
     *
     * 이미 FIFO가 존재하는 경우(EEXIST)는
     * 오류로 처리하지 않고 다음 단계로 진행한다.
     */
    if (mkfifo(SERVER_FIFO, 0600) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo");
            return 1;
        }
    }

    /*
     * 3. 서버 실행 정보 출력
     *
     * getpid(): 현재 서버 프로세스의 PID 반환
     * fflush(): 출력 버퍼의 내용을 즉시 화면에 표시
     */
    printf("===== FileGuard Server =====\n");
    printf("Server PID: %ld\n", (long)getpid());
    printf("FIFO: %s\n", SERVER_FIFO);
    printf("Waiting for requests...\n");
    fflush(stdout);

    /*
     * 4. FIFO 열기
     *
     * open(): FIFO를 읽기/쓰기 모드로 연다.
     * O_RDWR는 Linux 환경에서 FIFO를 열 때
     * 상대 프로세스 연결을 기다리지 않고 열 수 있다.
     *
     * 실제 요청 데이터 수신은 이후 단계에서 구현한다.
     */
    fd = open(SERVER_FIFO, O_RDWR);

    if (fd == -1) {
        perror("open");
        return 1;
    }

    /*
     * 5. 서버 실행 상태 유지
     *
     * pause(): 시그널이 발생할 때까지 프로세스를 대기시킨다.
     * SIGINT가 발생하면 handle_signal()에서 running을 0으로
     * 변경하고, 반복문을 종료한다.
     */
    while (running) {
        pause();
    }

    /*
     * 6. 서버 종료 및 자원 정리
     *
     * close(): FIFO 파일 디스크립터 닫기
     * unlink(): 생성한 FIFO 경로 삭제
     *
     * 프로그램 종료 시 불필요한 자원이
     * 남지 않도록 정리한다.
     */
    close(fd);
    unlink(SERVER_FIFO);

    printf("\nServer terminated.\n");

    return 0;
}

