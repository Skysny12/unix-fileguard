
#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <stddef.h>

// 새로운 파일 등록
int register_file(const char *filename);

// 파일 내용 조회
int read_file(const char *filename,
              char *buffer, size_t size);

// 파일 내용 수정 (이후 구현)
int update_file(const char *filename,
                const char *content);

#endif
