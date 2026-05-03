#pragma once

#include <QString>

class SpaceLookIpcClient
{
public:
    static bool sendPreviewRequest(const QString& filePath, QString* errorMessage = nullptr);
};
