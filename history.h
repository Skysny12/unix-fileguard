#ifndef HISTORY_H
#define HISTORY_H

// 파일 수정 전 자동 백업
int backup_file(int fd, const char *filename);

// 파일 변경 이력 조회
int list_history(const char *filename);

// 선택한 이전 버전으로 파일 복구
int restore_file(const char *filename, int version);


#endif

