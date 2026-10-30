#pragma once
#include <QtGlobal>

// Explicit Windows export/import for QObject classes crossing the lst_data DLL.
// Standalone radar test executables compile the source directly and leave this empty.
#if defined(LST_DATA_DLL_EXPORTS)
#define LST_DATA_API Q_DECL_EXPORT
#elif defined(LST_DATA_DLL_IMPORTS)
#define LST_DATA_API Q_DECL_IMPORT
#else
#define LST_DATA_API
#endif
