/*
	Created By: Hector Soto
	A wrapper for FTDI's D3XX library that implements a multi-threaded queue structure.
*/

#ifndef _QUEUED3XX_H
#define _QUEUED3XX_H

#include "FTD3XX.h" //Relies on the D3XX library being statically imported.

#ifdef _QUEUE_D3XX_EXPORT //Control dll importing/exporting of functions when building or including the library.
	#define HS_QD3XX_API __declspec(dllexport)
#else
	#define HS_QD3XX_API __declspec(dllimport)
#endif //_QUEUE_D3XX_EXPORT

#ifdef __cplusplus //Make our library compatible with C & C++.
	extern "C" {
#endif

typedef PVOID HS_QUEUE; //HS_Queue structure hidden within library to avoid user messing with it.

/*
	Returns version of the QueueD3XX library in hex. 0xAABBCCDD = Version AA.BB.CC.DD.
*/
HS_QD3XX_API ULONG HS_GetVersionQueueD3XX();

/*
	Wrapper for FT_Create(). So you don't need to import the D3XX library additionally to get a handle.
*/
HS_QD3XX_API FT_STATUS HS_Open(PVOID pvArg, DWORD dwFlags, FT_HANDLE *pftHandle);

/*
	Wrapper for FT_Close(). So you don't need to import the D3XX library additionally to close a handle.
*/
HS_QD3XX_API FT_STATUS HS_Close(FT_HANDLE ftHandle);

/*
	Creates a queue for a pipe on a new thread that immediately starts reading/writing.
*/
HS_QD3XX_API FT_STATUS HS_CreateQueue(FT_HANDLE Handle, UCHAR PipeID, ULONG StreamSize, ULONG QueueLength, BOOL Fixed, HS_QUEUE *NewQueueP);

/*
	Destroys a queue and its running thread.
*/
HS_QD3XX_API FT_STATUS HS_DestroyQueue(HS_QUEUE DQueue);

/*
	Copies data read from queue to ReadBuffer.
	Fails if queue is for an OUT pipe.
	Will destroy the queue if the pipe has been aborted and needs to undergo the abort procedure.
*/
HS_QD3XX_API FT_STATUS HS_ReadQueue(HS_QUEUE *Queue, PUCHAR ReadBuffer, PULONG BytesTransferred, BOOL Wait);

/*
	Copies data from WriteBuffer to queue.
	Fails if queue is for an IN pipe.
	Returns FT_BUSY if the queue is full.
	If Wait is true, HS_WriteQueue() won't return until WriteBuffer is copied into the queue.
*/
HS_QD3XX_API FT_STATUS HS_WriteQueue(HS_QUEUE Queue, PUCHAR WriteBuffer, BOOL Wait);

/*
	Get the status of the oldest write in the queue.
	Will destroy the queue if the pipe has been aborted and needs to undergo the abort procedure.
*/
HS_QD3XX_API FT_STATUS HS_GetWriteStatus(HS_QUEUE *Queue, PULONG BytesTransferred, BOOL Wait);

#ifdef __cplusplus
	}
#endif

#endif // !_QUEUED3XX_H