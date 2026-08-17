#ifndef LSTLIB_GLOBAL_H
#define LSTLIB_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(LSTLIB_LIBRARY)
#define LSTLIB_EXPORT Q_DECL_EXPORT
#else
#define LSTLIB_EXPORT Q_DECL_IMPORT
#endif

#endif // LSTLIB_GLOBAL_H
