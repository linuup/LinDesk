/****************************************************************************
** Meta object code from reading C++ file 'LinOCR_Mock.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../LinOCR_Mock.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qplugin.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'LinOCR_Mock.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_LinOCR_Mock_t {
    QByteArrayData data[1];
    char stringdata0[12];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_LinOCR_Mock_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_LinOCR_Mock_t qt_meta_stringdata_LinOCR_Mock = {
    {
QT_MOC_LITERAL(0, 0, 11) // "LinOCR_Mock"

    },
    "LinOCR_Mock"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_LinOCR_Mock[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

void LinOCR_Mock::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    Q_UNUSED(_c);
    Q_UNUSED(_a);
}

QT_INIT_METAOBJECT const QMetaObject LinOCR_Mock::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_LinOCR_Mock.data,
    qt_meta_data_LinOCR_Mock,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *LinOCR_Mock::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *LinOCR_Mock::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_LinOCR_Mock.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "IOCRPlugin"))
        return static_cast< IOCRPlugin*>(this);
    if (!strcmp(_clname, "com.lindesk.ILinPlugin/1.0"))
        return static_cast< ILinPlugin*>(this);
    if (!strcmp(_clname, "com.lindesk.IOCRPlugin/1.0"))
        return static_cast< IOCRPlugin*>(this);
    return QObject::qt_metacast(_clname);
}

int LinOCR_Mock::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}

QT_PLUGIN_METADATA_SECTION
static constexpr unsigned char qt_pluginMetaData[] = {
    'Q', 'T', 'M', 'E', 'T', 'A', 'D', 'A', 'T', 'A', ' ', '!',
    // metadata version, Qt version, architectural requirements
    0, QT_VERSION_MAJOR, QT_VERSION_MINOR, qPluginArchRequirements(),
    0xbf, 
    // "IID"
    0x02,  0x78,  0x1a,  'c',  'o',  'm',  '.',  'l', 
    'i',  'n',  'd',  'e',  's',  'k',  '.',  'I', 
    'L',  'i',  'n',  'P',  'l',  'u',  'g',  'i', 
    'n',  '/',  '1',  '.',  '0', 
    // "className"
    0x03,  0x6b,  'L',  'i',  'n',  'O',  'C',  'R', 
    '_',  'M',  'o',  'c',  'k', 
    // "MetaData"
    0x04,  0xa3,  0x6b,  'D',  'e',  's',  'c',  'r', 
    'i',  'p',  't',  'i',  'o',  'n',  0x78,  0x3b, 
    'S',  'i',  'm',  'p',  'l',  'e',  ' ',  'm', 
    'o',  'c',  'k',  ' ',  'p',  'l',  'u',  'g', 
    'i',  'n',  ' ',  'f',  'o',  'r',  ' ',  't', 
    'e',  's',  't',  'i',  'n',  'g',  ' ',  'L', 
    'i',  'n',  'D',  'e',  's',  'k',  ' ',  'p', 
    'l',  'u',  'g',  'i',  'n',  ' ',  'a',  'r', 
    'c',  'h',  'i',  't',  'e',  'c',  't',  'u', 
    'r',  'e',  '.',  0x64,  'K',  'e',  'y',  's', 
    0x81,  0x70,  'L',  'i',  'n',  'D',  'e',  's', 
    'k',  '_',  'O',  'C',  'R',  '_',  'M',  'o', 
    'c',  'k',  0x67,  'V',  'e',  'r',  's',  'i', 
    'o',  'n',  0x65,  '1',  '.',  '0',  '.',  '0', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN(LinOCR_Mock, LinOCR_Mock)

QT_WARNING_POP
QT_END_MOC_NAMESPACE
