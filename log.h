#ifndef LOG_H
#define LOG_H

// 파일 작업 내역 기록
// version은 복구 시 버전 번호, 일반 수정 시 0
int log_action(const char *action,
		const char *filename,
		int version);

#endif

