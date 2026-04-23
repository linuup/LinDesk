#pragma once

#include <QString>

class ListView;

class DebugInfoExporter
{
public:
    static bool exportToApplicationDir(ListView* listView, QString* outputPath = nullptr, QString* errorMessage = nullptr);
};
