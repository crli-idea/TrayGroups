#pragma once
#include <windows.h>
#include <psapi.h>
#include <vector>

namespace NativeResources {
struct Sample { SIZE_T working=0, privateCommit=0, privateWorking=0; double cpuSeconds=0; };
inline Sample Read() {
    Sample s;PROCESS_MEMORY_COUNTERS_EX p={};p.cb=sizeof(p);
    if(GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&p,sizeof(p))){s.working=p.WorkingSetSize;s.privateCommit=p.PrivateUsage;}
    SIZE_T size=sizeof(PSAPI_WORKING_SET_INFORMATION)+(s.working/4096+1024)*sizeof(PSAPI_WORKING_SET_BLOCK);
    std::vector<unsigned char> data(size);
    for(int attempt=0;attempt<3;attempt++){
        if(QueryWorkingSet(GetCurrentProcess(),data.data(),(DWORD)data.size())){
            auto w=(PSAPI_WORKING_SET_INFORMATION*)data.data();for(ULONG_PTR i=0;i<w->NumberOfEntries;i++)if(!w->WorkingSetInfo[i].Shared)s.privateWorking+=4096;break;
        }
        if(GetLastError()!=ERROR_BAD_LENGTH)break;data.resize(data.size()*2);
    }
    FILETIME a,b,k,u;if(GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u)){
        ULARGE_INTEGER kt,ut;kt.LowPart=k.dwLowDateTime;kt.HighPart=k.dwHighDateTime;ut.LowPart=u.dwLowDateTime;ut.HighPart=u.dwHighDateTime;s.cpuSeconds=(kt.QuadPart+ut.QuadPart)/10000000.0;
    }
    return s;
}
}
