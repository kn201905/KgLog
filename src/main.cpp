#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "KgLog.h"

#include <unistd.h>  // sleep のため


static KgLog  s_glog{4000, 200, "log_g", 2000, 500};

int main(void)
{
	char  str_buf[100];

	for (int i = 1; i <= 200; i++)
	{
		sprintf(str_buf, "書き込みテスト - %d", i);
		s_glog.Write(str_buf);
	}

	sleep(1);

	s_glog.Signal_ThrdStop();
	printf(s_glog.Get_StrInfo());

	return 0;
}
