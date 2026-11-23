#ifndef FILE_LOCK_H
#define FILE_LOCK_H

// 파일 잠금 획득
int lock_file(int fd);

// 파일 잠금 해제
int unlock_file(int fd);

#endif
