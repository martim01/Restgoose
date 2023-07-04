#pragma once

#ifdef _WIN32
    #ifdef RESTGOOSE_DLL
        #define RG_EXPORT __declspec(dllexport)
    #else
        #define RG_EXPORT __declspec(dllimport)
    #endif //
#else
    #define RG_EXPORT
#endif
