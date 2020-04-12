#include "IFxLogger.h"
#include <string>
#include <sstream>

int main()
{
	IFxLogger* log = IFxLogger::FxCreateLogger();
	log->Init("./log");

	std::string sz;
	std::stringstream ss;

	for (int i = 0; ; ++i)
	{
		if (ss.str().size() < 32 * 1024)
		{
			ss << i << ",";
		}
		log->LogText(ss.str().c_str());
		log->LogText("\n");
	}
	return 0;
}