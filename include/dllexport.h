#ifndef PML_RESTGOOSE_EXPORT_H
#define PML_RESTGOOSE_EXPORT_H

#ifdef _WIN32
    #ifdef RESTGOOSE_DLL
        #define RG_EXPORT __declspec(dllexport)
    #else
        #define RG_EXPORT __declspec(dllimport)
    #endif //
#else
    #define RG_EXPORT
#endif

#endif
