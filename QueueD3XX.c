
#ifdef _WIN32
#include "pch.h"
#endif //_WIN32
#include "QueueD3XX.h"
#include "HS_QueueD3XX.h"
#ifdef _WIN32 //For windows.
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
        _FreeQueueList(); //Not good practice. User should call HS_FreeQueueD3XX() on exit if needed.
        break;
    }
    return TRUE;
}
#else //For Linux/macOS.
    __attribute__((constructor))
    static void QueueD3XX_Init(void){_InitQueueList();}
    __attribute__((destructor))
    static void QueueD3XX_Free(void){_FreeQueueList();} //Not good practice. User should call HS_FreeQueueD3XX() on exit if needed.
#endif //_WIN32