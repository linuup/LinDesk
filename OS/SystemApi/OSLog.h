#pragma once
#include <QDebug>
#include <QString>
#include <stdarg.h>
#include <stdio.h>

inline void OS_LogDebug(const char* format, ...) {
    char buf[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format
<truncated 930 bytes>