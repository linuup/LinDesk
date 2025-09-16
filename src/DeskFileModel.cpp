#include "DeskFileModel.h"

DeskFileModel::DeskFileModel(const QString& name, const QString& path, const QString& iconPath)
    : m_name(name), m_path(path), m_iconPath(iconPath) {}

QString DeskFileModel::name() const { return m_name; }
QString DeskFileModel::path() const { return m_path; }
QString DeskFileModel::iconPath() const { return m_iconPath; }
void DeskFileModel::setName(const QString& name) { m_name = name; }
void DeskFileModel::setPath(const QString& path) { m_path = path; }
void DeskFileModel::setIconPath(const QString& iconPath) { m_iconPath = iconPath; }
