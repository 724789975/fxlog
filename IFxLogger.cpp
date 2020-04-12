#include "IFxLogger.h"
#include <assert.h>

#ifdef WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <time.h>
#include <io.h>
#include <fcntl.h>
#define Access _access
#ifndef PATH_MAX
#define PATH_MAX 256
#endif
typedef HANDLE FILE_HANDLE;
#else
#include <sys/mman.h> // for mmap and munmap
#include <sys/types.h> // for open
#include <sys/stat.h> // for open
#include <fcntl.h> // for open
#include <errno.h>
#include <unistd.h> // for lseek and write
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <string>
#define Access access
typedef int FILE_HANDLE;
#define SHARE_MEMORY_SIZE getpagesize()
#define INVALID_HANDLE_VALUE -1

#define GetLastError() errno
#endif

#define LOG_PAGS 4

#define MIN_MAXFILE_SIZE      1024*1024 // 可设置的最小文件大小为1M
#define MAX_MAXFILE_SIZE      1024*1024*1024 // 可设置的最大文件大小为1G

class ITrueLog
{
public:
	virtual ~ITrueLog() {}
	virtual bool							Init(const char* pszName, unsigned int dwMaxSize) = 0;
	virtual bool NewFile(const char* pszName, int dwFileNum) = 0;
	virtual bool							LogText(const char* pszLog) = 0;
	virtual bool							LogBinary(const char* pLog, unsigned int dwLen) = 0;
	virtual void							UnInit(void) = 0;
};

class CTrueShmLog : public ITrueLog
{
public:
	CTrueShmLog(void);
	~CTrueShmLog(void);
	bool									Init(const char* pszName, unsigned int dwMaxSize);
	bool NewFile(const char* pszName, int dwFileNum);
	void									UnInit(void);
	bool									LogText(const char* pszLog);
	bool									LogBinary(const char* pLog, unsigned int dwLen);

private:
	bool									_UpdateFilePointer();
	bool									_UpdateMapView(void);
	void									_CloseHandles(void);
private:

	FILE_HANDLE								m_hShmFile;				// 共享文件日志文件句柄
	FILE_HANDLE								m_hFileMapping;			// 共享文件日志文件句柄

	bool									m_bInited;				// 是否初始化
	char									m_szLogName[PATH_MAX];
	char									m_szFileName[PATH_MAX];
	
	unsigned int							m_dwMaxSize;			// 最大文件尺寸
	unsigned int							m_dwCurrentSize;		// 当前文件大小（不包括Map后增加的0）
	void*									m_pView;				// 映射
	char*									m_pPos;					// 当前位置指针
	unsigned int							m_dwOffsetInBlock;		// 在当前块中的偏移
	unsigned int							m_dwBlockSize;			// 块大小
	unsigned int							m_dwNowFileNum;			// 文件计数 第几个文件
};

CTrueShmLog::CTrueShmLog(void)
	: m_hShmFile(INVALID_HANDLE_VALUE)
	, m_hFileMapping(INVALID_HANDLE_VALUE)
	, m_dwMaxSize(0)						, m_dwCurrentSize(0)
	, m_bInited(false)						, m_pView(NULL)
	, m_pPos(NULL)							, m_dwOffsetInBlock(0)
	, m_dwBlockSize(0)						, m_dwNowFileNum(0)
{
	memset(m_szLogName, 0, PATH_MAX);
	memset(m_szFileName, 0, PATH_MAX);
}

CTrueShmLog::~CTrueShmLog(void) { }

bool CTrueShmLog::Init(const char* pszName, unsigned int dwMaxSize)
{
	if (true == m_bInited)
	{
		printf("[%s] Logger Inited.\n", __FUNCTION__);
		return false;
	}

	m_dwMaxSize = dwMaxSize;

#ifdef WIN32
	SYSTEM_INFO sSystem;
	GetSystemInfo(&sSystem);
	m_dwBlockSize = sSystem.dwAllocationGranularity;
#else
	m_dwBlockSize = SHARE_MEMORY_SIZE;
#endif

	sprintf(m_szFileName, "%s-%d.log", pszName, m_dwNowFileNum);
	while (-1 != Access(m_szFileName, 0))
	{
		++m_dwNowFileNum;
		sprintf(m_szFileName, "%s-%d.log", pszName, m_dwNowFileNum);
	}

	return NewFile(pszName, m_dwNowFileNum);
}

bool CTrueShmLog::NewFile(const char* pszName, int dwFileNum)
{
	if (true == m_bInited)
	{
		printf("[%s] Logger Inited.\n", __FUNCTION__);
		return false;
	}

	m_dwNowFileNum = dwFileNum;

	size_t nNameLen = strlen(pszName);
	if (nNameLen >= PATH_MAX)
	{
		printf("[%s] Name too long.\"%s\", length: %d.\n", __FUNCTION__, pszName, nNameLen);
		return false;
	}
	strcpy(m_szLogName, pszName);

	if (!_UpdateFilePointer())
	{
		printf("[%s] UpdatePointer failed!\n", __FUNCTION__);
		return false;
	}

	m_bInited = true;

	return true;
}

void CTrueShmLog::UnInit(void)
{
	if (!m_bInited)
	{
		printf("[%s] Logger is NOT Inited.\n", __FUNCTION__);
		return;
	}
	_CloseHandles();
}

bool CTrueShmLog::LogText(const char* pszLog)
{
	if (!m_bInited)
	{
		return false;
	}

	unsigned int dwLogLen = strlen(pszLog);

	return LogBinary(pszLog, dwLogLen);
}

bool CTrueShmLog::LogBinary(const char* pLog, unsigned int dwLen)
{
	if (!m_bInited)
	{
		return false;
	}

	if (dwLen >= m_dwBlockSize)
	{
		char logTmp[256] = { 0 };
		sprintf(logTmp, "[LOGGER] Log length bigger than blockSize: %d!", m_dwBlockSize);
		LogText(logTmp);
		return false;
	}

	// 必须转成int 否则出错
	while ((int)dwLen > ((int)m_dwMaxSize - (int)m_dwCurrentSize))
	{
		if (!_UpdateFilePointer())
		{
			printf("[%s] _UpdatePointer fail.\n", __FUNCTION__);
			return false;
		}
	}

	if (dwLen + m_dwOffsetInBlock > LOG_PAGS * m_dwBlockSize)
	{
		if (!_UpdateMapView())
		{
			printf("[%s] _UpdateMapView fail.\n", __FUNCTION__);
			return false;
		}
	}

	memcpy(m_pPos, pLog, dwLen);
	m_pPos += dwLen;
	m_dwCurrentSize += dwLen;
	m_dwOffsetInBlock += dwLen;

	return true;
}

bool CTrueShmLog::_UpdateFilePointer()
{
	// 检测是否由Init调用，如果不是，则需要关闭原来的句柄等
	if (INVALID_HANDLE_VALUE != m_hShmFile)
	{
		_CloseHandles();
	}

	sprintf(m_szFileName, "%s-%d.log", m_szLogName, m_dwNowFileNum);
	m_dwNowFileNum++;

#ifdef WIN32
	m_hShmFile = CreateFile(m_szFileName,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
#else
	m_hShmFile = open(m_szFileName, O_CREAT | O_RDWR | O_APPEND, 00777);
#endif

	if (INVALID_HANDLE_VALUE == m_hShmFile)
	{
		printf("[%s] CreateFile fail. Error: %d.\n", __FUNCTION__, GetLastError());
		return false;
	}

	UINT32 dwFileSizeLow = 0;
#ifdef WIN32
	DWORD dwFileSizeHigh = 0;
	dwFileSizeLow = GetFileSize(m_hShmFile, &dwFileSizeHigh);
#else
	struct stat sFstate;
	if (fstat(m_hShmFile, &sFstate))
	{
		printf("[%s] fstat fail.\n", __FUNCTION__);
		return false;
	}
	dwFileSizeLow = sFstate.st_size;
#endif
	// 设置文件大小
	m_dwCurrentSize = dwFileSizeLow;

	if (!_UpdateMapView())
	{
		printf("[%s] _UpdateMapView fail.\n", __FUNCTION__);
		return false;
	}

	return true;
}

bool CTrueShmLog::_UpdateMapView(void)
{
	if (NULL != m_pView)
	{
#ifdef WIN32
		UnmapViewOfFile(m_pView);
#else
		munmap(m_pView, 2 * m_dwBlockSize);
#endif
	}
#ifdef WIN32
	if (INVALID_HANDLE_VALUE != m_hFileMapping)
	{
		CloseHandle(m_hFileMapping);
	}
#endif

	m_dwOffsetInBlock = m_dwCurrentSize % m_dwBlockSize;

	int nOffset = m_dwCurrentSize / m_dwBlockSize * m_dwBlockSize;
	// 因为MapViewOfFile必须设置在Granularity的位置，所以要使用m_dwCurrentSize / m_nBlockSize
	// 为什么要乘以LOG_PAGS呢？嘿嘿，不告诉你~~
#ifdef WIN32
	m_hFileMapping = CreateFileMapping(m_hShmFile,
		NULL,
		PAGE_READWRITE,
		0, // 一直是0， 因为限制了最大文件大小，不需要用到此值
		nOffset + LOG_PAGS * m_dwBlockSize,
		m_szLogName);

	if (INVALID_HANDLE_VALUE == m_hFileMapping)
	{
		printf("[%s] CreateFileMapping fail. Error: %d.\n", __FUNCTION__, GetLastError());
		return false;
	}

	m_pView = MapViewOfFile(m_hFileMapping,
		FILE_MAP_ALL_ACCESS,
		0,
		nOffset,
		LOG_PAGS * m_dwBlockSize);
#else
	ftruncate(m_hShmFile, nOffset + LOG_PAGS * m_dwBlockSize);
	m_pView = (char*)mmap(NULL, LOG_PAGS * m_dwBlockSize, PROT_WRITE, MAP_SHARED, m_hShmFile, nOffset);
#endif

	if (NULL == m_pView)
	{
		printf("[%s] MapViewOfFile fail. Error: %d.\n", __FUNCTION__, GetLastError());
		return false;
	}
	m_pPos = (char*)m_pView + m_dwOffsetInBlock;
	return true;
}

void CTrueShmLog::_CloseHandles(void)
{
#ifdef WIN32
	UnmapViewOfFile(m_pView);
	CloseHandle(m_hFileMapping);
	// 需要加此句以删除结尾的0
	int fd = 0;
	fd = _open_osfhandle((long)m_hShmFile, O_RDWR);
	_chsize(fd, m_dwCurrentSize);
	CloseHandle(m_hShmFile);
#else
	munmap(m_pView, LOG_PAGS * m_dwBlockSize);
	// 需要加此句以删除结尾的0
	ftruncate(m_hShmFile, m_dwCurrentSize);
	close(m_hShmFile);
#endif
	m_pView = NULL;
	m_hShmFile = INVALID_HANDLE_VALUE;
	m_hFileMapping = INVALID_HANDLE_VALUE;
}

class CFxLogger :public IFxLogger
{
public:
	CFxLogger();
	// Interface of IFxLogger
	~CFxLogger();
	bool									Init(const char* pszName);
	bool									NewFile(const char* pszName);
	bool									LogText(const char* pText);
	bool									LogBinary(const char* pLog, unsigned int dwLen);
	void									Release();

private:
	char									m_szLogName[PATH_MAX];
	unsigned int							m_dwMaxSize;

	ITrueLog*								m_pNowLogger;
};

CFxLogger::CFxLogger()
	: m_pNowLogger(new CTrueShmLog)			, m_dwMaxSize(0)
{
	memset(m_szLogName, 0, PATH_MAX);
}

CFxLogger::~CFxLogger() { }

bool CFxLogger::Init(const char* pszName)
{
	m_dwMaxSize = HALF_GIGA;
	assert(m_dwMaxSize <= MAX_MAXFILE_SIZE);
	assert(m_dwMaxSize >= MIN_MAXFILE_SIZE);
	size_t nNameLen = strlen(pszName);
	if (nNameLen >= PATH_MAX)
	{
		return false;
	}

	strcpy(m_szLogName, pszName);

	return m_pNowLogger->Init(m_szLogName, m_dwMaxSize);
}

bool CFxLogger::NewFile(const char* pszName)
{
	size_t nNameLen = strlen(pszName);
	if (nNameLen >= PATH_MAX)
	{
		return false;
	}

	strcpy(m_szLogName, pszName);

	return m_pNowLogger->NewFile(m_szLogName, 0);
}

bool CFxLogger::LogText(const char* pText)
{
	return m_pNowLogger->LogText(pText);
}

bool CFxLogger::LogBinary(const char* pLog, unsigned int dwLen)
{
	return m_pNowLogger->LogBinary(pLog, dwLen);
}

void CFxLogger::Release()
{
	if (NULL == m_pNowLogger)
	{
		return;
	}
	m_pNowLogger->UnInit();
	delete m_pNowLogger;
	delete this;
}

IFxLogger* IFxLogger::FxCreateLogger()
{
	CFxLogger* pNewLogger = new CFxLogger;
	return pNewLogger;
}


