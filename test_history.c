#include <stdio.h>
#include "history.h"

int main(void) {
	int count = list_history("update_demo.txt");

    	if (count == -1)
       		return 1;

    	printf("\n총 %d개의 백업 버전이 있습니다.\n", count);

    	return 0;
}

