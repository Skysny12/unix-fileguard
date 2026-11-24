#include <stdio.h>
#include "history.h"
#include "file_manager.h"

int main(void) {
    char buffer[1024];

    // 저장된 버전 목록 확인
    if (list_history("update_demo.txt") == -1)
        return 1;

    // 첫 번째 백업 버전으로 복구
    if (restore_file("update_demo.txt", 1) == -1)
        return 1;

    // 복구된 파일 내용 확인
    if (read_file("update_demo.txt",
                  buffer, sizeof(buffer)) == -1)
        return 1;

    printf("복구된 내용: %s\n", buffer);

    return 0;
}

