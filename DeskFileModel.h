#pragma once
#include <QString>
#include <QList>


// 桌面文件数据模型，直接表示一个文件
class DeskFileModel {
public:
    DeskFileModel(const QString& name = QString(), const QString& path = QString(), const QString& iconPath = QString());
    QString name() const;
    QString path() const;
    QString iconPath() const;
    void setName(const QString& name);
    void setPath(const QString& path);
    void setIconPath(const QString& iconPath);
private:
    QString m_name;
    QString m_path;
    QString m_iconPath;
};
