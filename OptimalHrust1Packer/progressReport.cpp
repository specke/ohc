
#include "progressReport.h"
#include <memory.h>
#include <stdio.h>

ProgressReport::ProgressReport()
{
	memset(this, 0, sizeof(*this));
};

void ProgressReport::Report(int total, int done)
{
	int percents = done * 100 / total;
	if (printed) {
		printf("\r"); // move cursor back
	}
	printf("progress: %d%%  ", percents);
	printed = true;
};

void ProgressReport::Done()
{
	if (printed)
	{
		Report(1, 1);
		printf("\n");
	}
};
