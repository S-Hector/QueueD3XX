#include "QueueD3XX.h"
#include <stdio.h>

#define CHANNEL_COUNT 4
#define STREAM_SIZE 98 * 1024
#define FIXED_TRANSFER_SIZE FALSE
#define QUEUE_SIZE 100
#define LOOPS 20

int main()
{
    printf("Version: %i\n", HS_GetVersionQueueD3XX());
    FT_STATUS Status = FT_OK;
    FT_HANDLE Handle = 0;
    HS_QUEUE Queue[CHANNEL_COUNT*2];
    char ReadBuffer[CHANNEL_COUNT][STREAM_SIZE];
    ULONG BytesTransferred;
    Status = HS_Open(0, FT_OPEN_BY_INDEX, &Handle);
    if(Status != FT_OK){printf("ERROR: HS_Open returned %i\n", Status); return Status;}

    for(int i = 0; i < CHANNEL_COUNT; ++i)
    {
        Queue[i] = NULL;
        Queue[i + CHANNEL_COUNT] = NULL;
        Status = HS_CreateQueue(Handle, 0x02 + i, STREAM_SIZE, QUEUE_SIZE, FIXED_TRANSFER_SIZE, &Queue[i]);
        if(Status != FT_OK){printf("ERROR: HS_CreateQueue returned %i\n", Status); return Status;}
        Status = HS_CreateQueue(Handle, 0x82 + i, STREAM_SIZE, QUEUE_SIZE, FIXED_TRANSFER_SIZE, &Queue[i+CHANNEL_COUNT]);
        if(Status != FT_OK){printf("ERROR: HS_CreateQueue returned %i\n", Status); return Status;}
    }

    /*
    for(int i = 0; i < CHANNEL_COUNT; ++i)
    {
        printf("Destroy %i-A!\n", i);
        Status = HS_DestroyQueue(Queue[i]);
        if(Status != FT_OK){printf("ERROR: HS_DestroyQueue returned %i\n", Status); return Status;}
        printf("Destroy %i-B!\n", i);
        Status = HS_DestroyQueue(Queue[i+CHANNEL_COUNT]);
        if(Status != FT_OK){printf("ERROR: HS_DestroyQueue returned %i\n", Status); return Status;}
        printf("Destroy %i-C!\n", i);
    }*/
    HS_FreeQueueD3XX();

    Status = HS_Close(Handle);
    if(Status != FT_OK){printf("ERROR: HS_Close returned %i\n", Status); return Status;}
    printf("Made it here!\n");
    return 0;
}