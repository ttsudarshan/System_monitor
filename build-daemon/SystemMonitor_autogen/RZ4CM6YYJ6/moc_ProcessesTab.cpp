/****************************************************************************
** Meta object code from reading C++ file 'ProcessesTab.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../src/widgets/ProcessesTab.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ProcessesTab.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.4.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
namespace {
struct qt_meta_stringdata_ProcessesTab_t {
    uint offsetsAndSizes[18];
    char stringdata0[13];
    char stringdata1[14];
    char stringdata2[1];
    char stringdata3[12];
    char stringdata4[17];
    char stringdata5[16];
    char stringdata6[16];
    char stringdata7[16];
    char stringdata8[5];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_ProcessesTab_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_ProcessesTab_t qt_meta_stringdata_ProcessesTab = {
    {
        QT_MOC_LITERAL(0, 12),  // "ProcessesTab"
        QT_MOC_LITERAL(13, 13),  // "onKillProcess"
        QT_MOC_LITERAL(27, 0),  // ""
        QT_MOC_LITERAL(28, 11),  // "onKillGroup"
        QT_MOC_LITERAL(40, 16),  // "onSuspendProcess"
        QT_MOC_LITERAL(57, 15),  // "onResumeProcess"
        QT_MOC_LITERAL(73, 15),  // "onReniceProcess"
        QT_MOC_LITERAL(89, 15),  // "onFilterChanged"
        QT_MOC_LITERAL(105, 4)   // "text"
    },
    "ProcessesTab",
    "onKillProcess",
    "",
    "onKillGroup",
    "onSuspendProcess",
    "onResumeProcess",
    "onReniceProcess",
    "onFilterChanged",
    "text"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_ProcessesTab[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   50,    2, 0x08,    1 /* Private */,
       3,    0,   51,    2, 0x08,    2 /* Private */,
       4,    0,   52,    2, 0x08,    3 /* Private */,
       5,    0,   53,    2, 0x08,    4 /* Private */,
       6,    0,   54,    2, 0x08,    5 /* Private */,
       7,    1,   55,    2, 0x08,    6 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    8,

       0        // eod
};

Q_CONSTINIT const QMetaObject ProcessesTab::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ProcessesTab.offsetsAndSizes,
    qt_meta_data_ProcessesTab,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_ProcessesTab_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ProcessesTab, std::true_type>,
        // method 'onKillProcess'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onKillGroup'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSuspendProcess'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onResumeProcess'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onReniceProcess'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onFilterChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>
    >,
    nullptr
} };

void ProcessesTab::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ProcessesTab *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onKillProcess(); break;
        case 1: _t->onKillGroup(); break;
        case 2: _t->onSuspendProcess(); break;
        case 3: _t->onResumeProcess(); break;
        case 4: _t->onReniceProcess(); break;
        case 5: _t->onFilterChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *ProcessesTab::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ProcessesTab::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ProcessesTab.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ProcessesTab::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
