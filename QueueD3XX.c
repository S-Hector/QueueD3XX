// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "QueueD3XX.h"
#include "HS_QueueD3XX.h"
#include "stdio.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        _InitQueueList();
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        _FreeQueueList();
        break;
    }
    return TRUE;
}

