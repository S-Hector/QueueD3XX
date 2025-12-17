#ifdef _WIN32
    #include "pch.h"
    #include <processthreadsapi.h>
#else //For Linux/macOS
    #include "HS_processthreadsapi.h"
    #define HS_
#endif //_WIN32
#include <stdlib.h>
//#include <stdio.h>
#include <string.h>
#include "QueueD3XX.h"

#define QUEUE_D3XX_VERSION 0x0100000F

typedef struct _HS_Buffer{
    FT_STATUS Status; //Return value of the read/write pipe call.
    PUCHAR Buffer; //Buffer, must be freed by end user if not NULL.
    ULONG BytesTransferred; //Bytes transferred.
    OVERLAPPED Overlap; //Overlap for the buffer.
    struct _HS_Buffer *Next;
    struct _HS_Buffer *Prev;
} HS_Buffer;

typedef struct _Queue{
    FT_HANDLE Handle;
    UCHAR PipeID;
    ULONG StreamSize; //Size of read/write pipe calls.
    ULONG QueueLength; //Max size of the queue.
    ULONG Size; //Current size of the queue.
    ULONG SizeWS; //Size of WriteStatus.
    BOOL Active; //If true, a thread is actively using this queue.
    CRITICAL_SECTION ActiveMutex;
    CRITICAL_SECTION BuffersMutex;
    DWORD ThreadID;
    HANDLE ThreadHandle;
    struct _Queue *Prev;
    struct _Queue *Next;
    HS_Buffer *Buffers; //Our queue of buffers.
    HS_Buffer *WriteStatus; //Our queue of the status of past write pipe calls.
} HS_Queue;

HS_Queue *QueueList = NULL;
ULONG QueueSize = 0;
CRITICAL_SECTION QueueListMutex;

void _InitQueueList()
{
    QueueList = NULL;
    QueueSize = 0;
    InitializeCriticalSection(&QueueListMutex);
    //printf("INIT!\n");
}

void _FreeQueueList()
{
    //printf("EXIT!\n");
    if(!QueueList){return;}
    HS_Queue *Temp = QueueList;
    while(QueueList)
    {
        HS_DestroyQueue(QueueList);
    }
    DeleteCriticalSection(&QueueListMutex);
}

/*
    Add a buffer to the queue for read/write calls.
    If WriteData is not null, NewBuffer->Buffer points to it.
    If PNewBuffer is not null, NewBuffer address is written to *PNewBuffer. NULL on failure to add a buffer.
    Called by main and child threads. Increments read & write queues on success.
    EnterCritical must be FALSE if you're controlling the BuffersMutex outside the function.
*/
FT_STATUS _AddBuffer(HS_Queue *Queue, PUCHAR WriteData, HS_Buffer **PNewBuffer, BOOL EnterCritical)
{
    if(EnterCritical){EnterCriticalSection(&Queue->BuffersMutex);}
    if(PNewBuffer){*PNewBuffer = NULL;}
    if((Queue->Size + Queue->SizeWS) >= Queue->QueueLength) //Queue max length must not be surpassed.
    {
        if(EnterCritical){LeaveCriticalSection(&Queue->BuffersMutex);}
        return FT_BUSY; //User needs to wait until queue gains space.
    }
    HS_Buffer *NewBuffer = malloc(sizeof(HS_Buffer));
    if(!NewBuffer){if(EnterCritical){LeaveCriticalSection(&Queue->BuffersMutex);} return FT_NO_SYSTEM_RESOURCES;}
    if(WriteData) //If we're assigned data to being written out.
    {
        NewBuffer->Buffer = WriteData; //Assign data to write out.
    }
    else //Create buffer to hold incoming data.
    {
        NewBuffer->Buffer = malloc(Queue->StreamSize);
        if(!NewBuffer->Buffer)
        {
            free(NewBuffer);
            if(EnterCritical){LeaveCriticalSection(&Queue->BuffersMutex);} return FT_NO_SYSTEM_RESOURCES;
        }
    }
    if(FT_InitializeOverlapped(Queue->Handle,&NewBuffer->Overlap) != FT_OK)
    {
        free(NewBuffer->Buffer); free(NewBuffer);
        if(EnterCritical){LeaveCriticalSection(&Queue->BuffersMutex);} return FT_NO_SYSTEM_RESOURCES;
    }
    NewBuffer->Status = FT_IO_PENDING; //Waiting for read/write call to happen or finish.

    if(!Queue->Buffers) //Create Buffer list.
    {
        Queue->Buffers = NewBuffer;
        Queue->Buffers->Prev = NewBuffer;
        Queue->Buffers->Next = NewBuffer;
    }
    else
    {
        //Insert NewBuffer into Buffer list as last item.
        Queue->Buffers->Prev->Next = NewBuffer;
        NewBuffer->Next = Queue->Buffers;
        NewBuffer->Prev = Queue->Buffers->Prev;
        Queue->Buffers->Prev = NewBuffer;
    }
    Queue->Size += 1;
    if(PNewBuffer){*PNewBuffer = NewBuffer;}
    if(EnterCritical){LeaveCriticalSection(&Queue->BuffersMutex);}
    return FT_OK;
}

/*
    Removes the oldest buffer in the queue. For read queues only.
    Assumes queue is not empty.
    //Only called by main thread. Decrements read queues.
*/
FT_STATUS _DestroyBuffer(HS_Queue *Queue)
{
    EnterCriticalSection(&Queue->BuffersMutex);
    HS_Buffer *Temp = Queue->Buffers; //Temp is equal to oldest buffer.
    FT_ReleaseOverlapped(Queue->Handle, &Temp->Overlap); //Release the overlap.
    if(Queue->Size == 1)
    {
        free(Queue->Buffers->Buffer); //Free buffer, it should exist.
        free(Queue->Buffers);
        Queue->Buffers = NULL;
    }
    else
    {
        if(Temp == Queue->Buffers){Queue->Buffers = Temp->Next;}
        Temp->Prev->Next = Temp->Next;
        Temp->Next->Prev = Temp->Prev;
        free(Temp->Buffer); //Free buffer, it should exist.
        free(Temp);
    }
    Queue->Size -= 1;
    LeaveCriticalSection(&Queue->BuffersMutex);
    return FT_OK;
}

/*
    Retrieves the oldest buffer in the queue. For write queues.
    Moves the oldest buffer to the WriteStatus queue.
    Assumes queue is not empty.
    Only called by child thread. Decrements write queues.
*/
FT_STATUS _RetrieveBuffer(HS_Queue *Queue)
{
    EnterCriticalSection(&Queue->BuffersMutex);
    HS_Buffer *Temp = Queue->Buffers; //Temp is equal to oldest buffer.
    FT_ReleaseOverlapped(Queue->Handle,&Temp->Overlap); //Release the overlap.
    //Remove oldest buffer from queue.
    if(Queue->Size == 1)
    {
        Queue->Buffers = NULL;
    }
    else
    {
        if(Temp == Queue->Buffers){Queue->Buffers = Temp->Next;}
        Temp->Prev->Next = Temp->Next;
        Temp->Next->Prev = Temp->Prev;
    }
    Queue->Size -= 1;
    //Add oldest buffer to WriteStatus queue.
    if(!Queue->WriteStatus) //Create WriteStatus queue.
    {
        Queue->WriteStatus = Temp;
        Queue->WriteStatus->Prev = Temp;
        Queue->WriteStatus->Next = Temp;
    }
    else
    {
        //Insert oldest buffer into Queue->WriteStatus as last item.
        Queue->WriteStatus->Prev->Next = Temp;
        Temp->Next = Queue->WriteStatus;
        Temp->Prev = Queue->WriteStatus->Prev;
        Queue->WriteStatus->Prev = Temp;
    }
    Queue->SizeWS += 1; //Size of WriteStatus queue increased.
    LeaveCriticalSection(&Queue->BuffersMutex);
    return FT_OK;
}

/*
    Called by _QueueRequester, frees all buffers.
    Overlaps must complete or pipes aborted. Otherwise freeing them is not valid.
*/
void _FreeBuffers(HS_Queue *Queue)
{
    HS_Buffer *Temp = NULL;
    EnterCriticalSection(&Queue->BuffersMutex);
    if(Queue->Buffers){Temp = Queue->Buffers->Next;}
    FT_AbortPipe(Queue->Handle,Queue->PipeID); //Abort the pipe.
    while(Queue->Buffers) //While Queue->Buffers exists.
    {
        FT_ReleaseOverlapped(Queue->Handle, &Temp->Overlap); //Release the overlap.
        free(Temp->Buffer); //Should exist.
        if(Temp == Queue->Buffers){free(Temp); Queue->Buffers = NULL; break;} //Deleted all buffers.
        Queue->Buffers->Prev = Temp->Next; //Using as placeholder. Prev no longer matters.
        free(Temp); //Free Temp.
        Temp = Queue->Buffers->Prev; //Temp = Temp->Next;
    }
    if(Queue->WriteStatus){Temp = Queue->WriteStatus->Next;}
    while(Queue->WriteStatus) //While Queue->WriteStatus exists.
    {
        FT_ReleaseOverlapped(Queue->Handle, &Temp->Overlap); //Release the overlap.
        free(Temp->Buffer); //Should exist.
        if(Temp == Queue->WriteStatus){free(Temp); Queue->WriteStatus = NULL; break;} //Deleted all buffers.
        Queue->WriteStatus->Prev = Temp->Next; //Using as placeholder. Prev no longer matters.
        free(Temp); //Free Temp.
        Temp = Queue->WriteStatus->Prev; //Temp = Temp->Next;
    }
    Queue->Size = 0;
    LeaveCriticalSection(&Queue->BuffersMutex);
    return;
}

/*
    Makes read/write pipe requests and fills the queue.
*/
FT_STATUS _QueueRequester(HS_Queue *Queue)
{
    HS_Buffer *TempBuffer = NULL;
    FT_STATUS Status = FT_OK;
    BOOL InPipe = Queue->PipeID & 0x80; //If true, we make read pipe requests.
    while(TRUE)
    {
        if(TryEnterCriticalSection(&Queue->ActiveMutex))
        {
            if(!Queue->Active) //Main thread is telling us to stop.
            {
                LeaveCriticalSection(&Queue->ActiveMutex);
                //Clear out the buffer!
                _FreeBuffers(Queue);
                return FT_OK; //Stop running.
            }
            LeaveCriticalSection(&Queue->ActiveMutex);
        }
        if(InPipe) //Make read pipe requests.
        {
            EnterCriticalSection(&Queue->BuffersMutex);
            _AddBuffer(Queue, NULL, &TempBuffer, FALSE); //Add a buffer to the read pipe queue.
            if(TempBuffer) //If a buffer was added, initiate the read pipe call for it.
            {
                #ifdef _WIN32
                    TempBuffer->Status = FT_ReadPipe(Queue->Handle, Queue->PipeID,
                #else
                    TempBuffer->Status = FT_ReadPipeAsync(Queue->Handle, Queue->PipeID & 0x07, //Linux uses FIFO ID.
                #endif //_WIN32
                                                    TempBuffer->Buffer, Queue->StreamSize,
                                                    &TempBuffer->BytesTransferred, &TempBuffer->Overlap);
            }
            TempBuffer = NULL;
            LeaveCriticalSection(&Queue->BuffersMutex);
        }
        else //Make write pipe requests.
        {
            EnterCriticalSection(&Queue->BuffersMutex);
            if(Queue->Size) //If there's data to write out.
            {
                #ifdef _WIN32
                    Queue->Buffers->Status = FT_WritePipe(Queue->Handle, Queue->PipeID,
                #else
                    Queue->Buffers->Status = FT_WritePipeAsync(Queue->Handle, Queue->PipeID & 0x07, //Linux uses FIFO ID.
                #endif //_WIN32
                                                        Queue->Buffers->Buffer, Queue->StreamSize,
                                                        &Queue->Buffers->BytesTransferred,&Queue->Buffers->Overlap);
                LeaveCriticalSection(&Queue->BuffersMutex);
                _RetrieveBuffer(Queue); //Decrement write queue and increment WriteStatus queue.
            }
            else
            {
                LeaveCriticalSection(&Queue->BuffersMutex);
            }
        }
    }
}

/*
    Creates the thread for the queue.
*/
FT_STATUS _CreateThread(HS_Queue *Queue)
{
    if(!Queue){return FT_INVALID_PARAMETER;}
    Queue->Active = TRUE; //Indicate Queue is active.
    InitializeCriticalSection(&Queue->ActiveMutex);
    InitializeCriticalSection(&Queue->BuffersMutex);
    #ifdef _WIN32
        Queue->ThreadHandle = CreateThread(NULL, 0, (PVOID)_QueueRequester, Queue, 0, &Queue->ThreadID);
    #else
        Queue->ThreadHandle = (HANDLE) malloc(sizeof(pthread_t));
        if(Queue->ThreadHandle)
        {
            if(pthread_create(Queue->ThreadHandle, NULL, (PVOID)_QueueRequester, Queue))
            {free(Queue->ThreadHandle); Queue->ThreadHandle = NULL;} //Failed to create thread.
        }
    #endif //_WIN32
    if(!Queue->ThreadHandle) //If we failed to make a thread.
    {
        Queue->Active = FALSE;
        DeleteCriticalSection(&Queue->ActiveMutex);
        DeleteCriticalSection(&Queue->BuffersMutex);
        return FT_NO_SYSTEM_RESOURCES;
    }
    return FT_OK;
}

//Returns version of the QueueD3XX library in hex. 0xAABBCCDD = Version AA.BB.CC.DD.
ULONG HS_GetVersionQueueD3XX(){return (ULONG)QUEUE_D3XX_VERSION;}

/*
    Add a new Queue to the Queue list.
*/
FT_STATUS AddQueue(FT_HANDLE Handle, UCHAR PipeID, ULONG StreamSize,ULONG QueueLength, PVOID NewQueueP)
{
    EnterCriticalSection(&QueueListMutex); //Wait until we can enter the queue list mutex.
    FT_STATUS Status;
    HS_Queue *Temp = QueueList;
    if((StreamSize < 1) || (QueueLength < 1) || (!NewQueueP)){LeaveCriticalSection(&QueueListMutex); return FT_INVALID_PARAMETER;}
    if(QueueList)
    {
        do
        {
            if((Temp->Handle == Handle) && (Temp->PipeID == PipeID))
            {
                LeaveCriticalSection(&QueueListMutex); return FT_RESERVED_PIPE;
            } //Return if a queue already exists for the given pipe & handle.
            Temp = Temp->Next;
        }while(Temp != QueueList);
    }
    HS_Queue *NewQueue = malloc(sizeof(HS_Queue));
    if(!NewQueue){LeaveCriticalSection(&QueueListMutex); return FT_NO_SYSTEM_RESOURCES;}
    *((PVOID *)NewQueueP) = NewQueue;
    NewQueue->Handle = Handle;
    NewQueue->PipeID = PipeID;
    NewQueue->StreamSize = StreamSize;
    NewQueue->QueueLength = QueueLength;
    NewQueue->Size = 0;
    NewQueue->SizeWS = 0;
    NewQueue->Active = FALSE;
    NewQueue->Buffers = NULL;
    NewQueue->WriteStatus = NULL;
    NewQueue->Prev = NULL; NewQueue->Next = NULL;
    if(!QueueList) //Create QueueList.
    {
        QueueList = NewQueue;
        QueueList->Prev = NewQueue;
        QueueList->Next = NewQueue;
    }
    else
    {
        //Insert NewQueue into QueueList as last item.
        QueueList->Prev->Next = NewQueue;
        NewQueue->Next = QueueList;
        NewQueue->Prev = QueueList->Prev;
        QueueList->Prev = NewQueue;
    }
    QueueSize += 1;
    Status = _CreateThread(NewQueue); //Create a new thread for the queue.
    LeaveCriticalSection(&QueueListMutex);
    if(Status != FT_OK){HS_DestroyQueue(NewQueue); return Status;}
    return Status;
}

/*
    Wrapper for FT_Create(). So you don't need to import the D3XX library additionally to get a handle.
*/
HS_QD3XX_API FT_STATUS HS_Open(PVOID pvArg,DWORD dwFlags,FT_HANDLE *pftHandle)
{
    return FT_Create(pvArg, dwFlags, pftHandle);
}

/*
    Wrapper for FT_Close(). So you don't need to import the D3XX library additionally to close a handle.
*/
HS_QD3XX_API FT_STATUS HS_Close(FT_HANDLE ftHandle)
{
    return FT_Close(ftHandle);
}

HS_QD3XX_API FT_STATUS HS_CreateQueue(FT_HANDLE Handle, UCHAR PipeID, ULONG StreamSize, ULONG QueueLength, BOOL Fixed, HS_QUEUE *NewQueueP)
{
    FT_STATUS Status = FT_OK;
    if(Fixed){Status = FT_SetStreamPipe(Handle, FALSE, FALSE, PipeID, StreamSize);}
    else{Status = FT_ClearStreamPipe(Handle, FALSE, FALSE, PipeID);}
    if(Status != FT_OK){return Status;}
    Status = AddQueue(Handle, PipeID, StreamSize, QueueLength, NewQueueP);
    return Status;
}

HS_QD3XX_API FT_STATUS HS_DestroyQueue(HS_QUEUE DQueue)
{
    EnterCriticalSection(&QueueListMutex);
    HS_Queue *Temp = DQueue;
    if(!DQueue){LeaveCriticalSection(&QueueListMutex); return FT_INVALID_PARAMETER;}
    if(!QueueList){LeaveCriticalSection(&QueueListMutex); return FT_NO_MORE_ITEMS;}
    if(Temp->Active) //Kill the queue's thread.
    {
        EnterCriticalSection(&Temp->ActiveMutex);
        Temp->Active = FALSE; //Tell thread to stop.
        LeaveCriticalSection(&Temp->ActiveMutex);
        #ifdef _WIN32
            WaitForSingleObject(Temp->ThreadHandle, INFINITE); //Wait for thread to stop.
            CloseHandle(Temp->ThreadHandle); //Close the thread to free up resources.
        #else
            pthread_join(*((pthread_t *)Temp->ThreadHandle), NULL);
            free(Temp->ThreadHandle); Temp->ThreadHandle = NULL;
        #endif //_WIN32
    }
    if(QueueSize == 1)
    {
        free(QueueList);
        QueueList = NULL;
    }
    else
    {
        if(Temp == QueueList){QueueList = Temp->Next;}
        Temp->Prev->Next = Temp->Next;
        Temp->Next->Prev = Temp->Prev;
        free(Temp);
    }
    QueueSize -= 1;
    LeaveCriticalSection(&QueueListMutex);
    return FT_OK;
}

/*
    Copies data read from queue to ReadBuffer.
    Fails if queue is for an OUT pipe.
    Will destroy the queue if the pipe has been aborted and needs to undergo the abort procedure.
*/
HS_QD3XX_API FT_STATUS HS_ReadQueue(HS_QUEUE *Queue, PUCHAR ReadBuffer, PULONG BytesTransferred, BOOL Wait)
{
    FT_STATUS Status;
    if((!Queue) || (!ReadBuffer) || (!BytesTransferred)){return FT_INVALID_PARAMETER;}
    if(!(*Queue)){return FT_INVALID_PARAMETER;}
    HS_Queue *Temp = *Queue;
    HS_Buffer *TempBuffer = NULL;
    if(!(Temp->PipeID & 0x80)){return FT_INVALID_PARAMETER;} //Return if queue is for a OUT pipe.
    EnterCriticalSection(&Temp->BuffersMutex);
    while(Temp->Size < 1)
    {
        LeaveCriticalSection(&Temp->BuffersMutex);
        if(!Wait){return FT_NO_MORE_ITEMS;}
        EnterCriticalSection(&Temp->BuffersMutex);
    }
    TempBuffer = Temp->Buffers; //Get oldest buffer in queue.
    LeaveCriticalSection(&Temp->BuffersMutex);
    if((TempBuffer->Status != FT_IO_PENDING) && (TempBuffer->Status != FT_OK))
    { //If the read pipe call failed, destroy the queue.
        Status = TempBuffer->Status; //Keep status.
        *Queue = NULL; //Set the Queue to NULL so the user doesn't try to use it.
        HS_DestroyQueue(Temp); //Destroy the queue.
        return Status;
    }
    Status = FT_GetOverlappedResult(Temp->Handle, &TempBuffer->Overlap,
                                    &TempBuffer->BytesTransferred, Wait); //Wait for result.
    *BytesTransferred = TempBuffer->BytesTransferred; //Set bytes transferred.
    memcpy(ReadBuffer, TempBuffer->Buffer, TempBuffer->BytesTransferred);
    if(Status == FT_OK)
    {
        _DestroyBuffer(Temp);
        return FT_OK;
    }
    if((Status != FT_IO_INCOMPLETE) && (Status != FT_IO_PENDING))
    {
        *Queue = NULL; //Set the Queue to NULL so the user doesn't try to use it.
        HS_DestroyQueue(Temp); //Destroy the queue.
    }
    return Status;
}

/*
    Copies data from WriteBuffer to queue.
    Fails if queue is for an IN pipe.
    Returns FT_BUSY if the queue is full.
    If Wait is true, HS_WriteQueue() won't return until WriteBuffer is copied into the queue.
*/
HS_QD3XX_API FT_STATUS HS_WriteQueue(HS_QUEUE Queue, PUCHAR WriteBuffer, BOOL Wait)
{
    FT_STATUS Status;
    if((!Queue) || (!WriteBuffer)){return FT_INVALID_PARAMETER; }
    HS_Queue *Temp = Queue;
    if(Temp->PipeID & 0x80){return FT_INVALID_PARAMETER;} //Return if queue is for an IN pipe.
    HS_Buffer *TempBuffer = NULL;
    PUCHAR NewBuffer = malloc(Temp->StreamSize); //Allocate buffer to hold WriteBuffer data.
    if(!NewBuffer){return FT_NO_SYSTEM_RESOURCES;} //Failed to allocate memory.
    memcpy(NewBuffer, WriteBuffer, Temp->StreamSize); //Copy data.
    while(!TempBuffer)
    {
        Status = _AddBuffer(Temp, NewBuffer, &TempBuffer, TRUE); //Add NewBuffer to queue.
        if(!Wait & !TempBuffer){free(NewBuffer); break;} //If we're not waiting, free the buffer and break.
    }
    return Status;
}

/*
    Get the status of the oldest write in the queue.
*/
HS_QD3XX_API FT_STATUS HS_GetWriteStatus(HS_QUEUE *Queue, PULONG BytesTransferred, BOOL Wait)
{
    FT_STATUS Status;
    if((!Queue) || (!BytesTransferred)){return FT_INVALID_PARAMETER;}
    if(!(*Queue)){return FT_INVALID_PARAMETER;}
    HS_Queue *Temp = *Queue;
    HS_Buffer *TempBuffer = NULL;
    if(Temp->PipeID & 0x80){return FT_INVALID_PARAMETER;} //Return if queue is for an IN pipe.
    do
    {
        EnterCriticalSection(&Temp->BuffersMutex);
        if(!Temp->SizeWS) //If no write pipe calls have happened.
        {
            Status = Temp->Size ? FT_IO_PENDING : FT_NO_MORE_ITEMS;
            //^No writes have been queued up or we're waiting for a write to happen.
        }
        else
        {
            Status = FT_OK; //Write call has happened, we can now get overlap.
            LeaveCriticalSection(&Temp->BuffersMutex);
            break;
        }
        LeaveCriticalSection(&Temp->BuffersMutex);
    }while(Wait && (Status != FT_NO_MORE_ITEMS));
    if(Status == FT_OK) //Temp->WriteStatus exists.
    {
        EnterCriticalSection(&Temp->BuffersMutex);
        TempBuffer = Temp->WriteStatus;
        LeaveCriticalSection(&Temp->BuffersMutex);
        do
        {
            Status = FT_GetOverlappedResult(Temp->Handle, &TempBuffer->Overlap,
                                                            &TempBuffer->BytesTransferred, FALSE);
        }while(Wait && (Status != FT_OK));
        *BytesTransferred = TempBuffer->BytesTransferred;
        if(Status == FT_OK) //Destroy buffer as we got its status.
        {
            EnterCriticalSection(&Temp->BuffersMutex);
            if(Temp->SizeWS == 1)
            {
                free(Temp->WriteStatus->Buffer);
                free(Temp->WriteStatus);
                Temp->WriteStatus = NULL;
            }
            else //More than one buffer exists.
            {
                if(TempBuffer == Temp->WriteStatus){Temp->WriteStatus = TempBuffer->Next;}
                TempBuffer->Prev->Next = TempBuffer->Next;
                TempBuffer->Next->Prev = TempBuffer->Prev;
                free(TempBuffer->Buffer);
                free(TempBuffer);
            }
            Temp->SizeWS -= 1;
            LeaveCriticalSection(&Temp->BuffersMutex);
            return FT_OK;
        }
        if((Status != FT_IO_INCOMPLETE) && (Status != FT_IO_PENDING))
        {
            *Queue = NULL; //Set the Queue to NULL so the user doesn't try to use it.
            HS_DestroyQueue(Temp); //Destroy the queue.
        }
    }
    return Status;
}