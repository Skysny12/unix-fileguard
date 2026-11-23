#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "file_manager.h"

int main(void) {
    char buffer[1024];

    // 테스트용 파일 등록
    if (register_file("test.txt") == -1)
        return 1;

    printf("파일 등록 성공!\n");

    // 테스트용 내용 작성
    int fd = open("test.txt", O_WRONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    const char *text = "Hello UNIX Project!";
    if (write(fd, text, strlen(text)) == -1) {
        perror("write");
        close(fd);
        return 1;
    }
    close(fd);

    // 파일 내용 조회
    if (read_file("test.txt", buffer, sizeof(buffer)) == -1)
        return 1;

    printf("파일 내용: %s\n", buffer);

    return 0;
}

