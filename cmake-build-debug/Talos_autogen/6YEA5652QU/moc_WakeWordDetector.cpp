/****************************************************************************
** Meta object code from reading C++ file 'WakeWordDetector.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../include/WakeWordDetector.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'WakeWordDetector.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_WakeWordDetector_t {
    uint offsetsAndSizes[14];
    char stringdata0[17];
    char stringdata1[17];
    char stringdata2[1];
    char stringdata3[11];
    char stringdata4[18];
    char stringdata5[19];
    char stringdata6[6];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_WakeWordDetector_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_WakeWordDetector_t qt_meta_stringdata_WakeWordDetector = {
    {
        QT_MOC_LITERAL(0, 16),  // "WakeWordDetector"
        QT_MOC_LITERAL(17, 16),  // "wakeWordDetected"
        QT_MOC_LITERAL(34, 0),  // ""
        QT_MOC_LITERAL(35, 10),  // "confidence"
        QT_MOC_LITERAL(46, 17),  // "processAudioChunk"
        QT_MOC_LITERAL(64, 18),  // "std::vector<float>"
        QT_MOC_LITERAL(83, 5)   // "chunk"
    },
    "WakeWordDetector",
    "wakeWordDetected",
    "",
    "confidence",
    "processAudioChunk",
    "std::vector<float>",
    "chunk"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_WakeWordDetector[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       2,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   26,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       4,    1,   29,    2, 0x0a,    3 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Float,    3,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 5,    6,

       0        // eod
};

Q_CONSTINIT const QMetaObject WakeWordDetector::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_WakeWordDetector.offsetsAndSizes,
    qt_meta_data_WakeWordDetector,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_WakeWordDetector_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<WakeWordDetector, std::true_type>,
        // method 'wakeWordDetected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'processAudioChunk'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::vector<float> &, std::false_type>
    >,
    nullptr
} };

void WakeWordDetector::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<WakeWordDetector *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->wakeWordDetected((*reinterpret_cast< std::add_pointer_t<float>>(_a[1]))); break;
        case 1: _t->processAudioChunk((*reinterpret_cast< std::add_pointer_t<std::vector<float>>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (WakeWordDetector::*)(float );
            if (_t _q_method = &WakeWordDetector::wakeWordDetected; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *WakeWordDetector::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *WakeWordDetector::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_WakeWordDetector.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int WakeWordDetector::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void WakeWordDetector::wakeWordDetected(float _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
