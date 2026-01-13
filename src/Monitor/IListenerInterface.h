// IListener.h - 直接在接口中实现公共功能
#pragma once
#include <QObject>
#include <QDebug>
#include <atomic>
#include <memory>



class IListenerInterface : public QObject {
    Q_OBJECT

public:
    IListenerInterface(const QString& name, const QString& type, QObject* parent = nullptr)
        : QObject(parent), m_name(name), m_type(type), m_listening(false) {}

    virtual ~IListenerInterface() {

    }
    // 纯虚函数 - 必须由子类实现
    virtual bool startListening() = 0;
    virtual void stopListening() = 0;

    // 公共功能 - 直接实现
    bool isListening() const { return m_listening.load(); }
    QString getName() const { return m_name; }
    QString getType() const { return m_type; }

protected:
    // 保护方法供子类使用
    void setListening(bool listening) { m_listening.store(listening); }

    void logMessage(const QString& message) const {
        qDebug() << QString("[%1|%2] %3").arg(m_type, m_name, message);
    }

    void emitError(const QString& error) {
        emit errorOccurred(m_name, error);
    }

signals:
    void errorOccurred(const QString& listenerName, const QString& errorMessage);

private:
    QString m_name;
    QString m_type;
    std::atomic<bool> m_listening;
};