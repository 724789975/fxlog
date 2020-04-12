#ifndef __IFxLogger_H__
#define __IFxLogger_H__

#define HALF_GIGA    512*1024*1024
class IFxLogger
{
public:
    virtual  ~IFxLogger() {}

    virtual bool Init(const char* pszName) = 0;

    virtual bool NewFile(const char* pszName) = 0;

    virtual bool LogText(const char* pszLog) = 0;

    virtual bool LogBinary(const char* pLog, unsigned int dwLen) = 0;

    virtual void Release(void) = 0;


	static IFxLogger* FxCreateLogger();
};


#endif
