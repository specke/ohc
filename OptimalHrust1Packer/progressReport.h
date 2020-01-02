
#pragma once

class ProgressReport
{
	bool printed;

public:
	ProgressReport();
	void Report(int total, int done);
	void Done();
};
