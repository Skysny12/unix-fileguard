#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "file_manager.h"

int main(void) {
    char buffer[1024];

    // 테스트 파일 준비
    int fd = open("update_demo.txt",
                  O_WRONLY | O_CREAT, 0644);

    if (fd == -1) {
        perror("파일 생성 실패");
        return 1;
    }
    close(fd);

    // 파일 수정
    if (update_file("update_demo.txt",
                    "Hello FileGuard!") == -1)
        return 1;

    printf("파일 수정 성공!\n");

    // 수정된 내용 확인
    if (read_file("update_demo.txt",
                  buffer, sizeof(buffer)) == -1)
        return 1;

    printf("수정된 내용: %s\n", buffer);

    return 0;
}

