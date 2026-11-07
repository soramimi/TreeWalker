/****************************************************************************
** Meta object code from reading C++ file 'IncrementalSearchPlugin.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/IncrementalSearchPlugin.h"
#include <QtCore/qmetatype.h>
#include <QtCore/qplugin.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'IncrementalSearchPlugin.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.12.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN23IncrementalSearchPluginE_t {};
} // unnamed namespace

template <> constexpr inline auto IncrementalSearchPlugin::qt_create_metaobjectdata<qt_meta_tag_ZN23IncrementalSearchPluginE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "IncrementalSearchPlugin"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<IncrementalSearchPlugin, qt_meta_tag_ZN23IncrementalSearchPluginE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject IncrementalSearchPlugin::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN23IncrementalSearchPluginE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN23IncrementalSearchPluginE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN23IncrementalSearchPluginE_t>.metaTypes,
    nullptr
} };

void IncrementalSearchPlugin::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<IncrementalSearchPlugin *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *IncrementalSearchPlugin::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *IncrementalSearchPlugin::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN23IncrementalSearchPluginE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "IncrementalSearchInterface"))
        return static_cast< IncrementalSearchInterface*>(this);
    if (!strcmp(_clname, "soramimi.jp.IncrementalSearchPlugin/1.0"))
        return static_cast< IncrementalSearchInterface*>(this);
    return QObject::qt_metacast(_clname);
}

int IncrementalSearchPlugin::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}

#ifdef QT_MOC_EXPORT_PLUGIN_V2
static constexpr unsigned char qt_pluginMetaDataV2_IncrementalSearchPlugin[] = {
    0xbf, 
    // "IID"
    0x02,  0x78,  0x23,  's',  'o',  'r',  'a',  'm', 
    'i',  'm',  'i',  '.',  'j',  'p',  '.',  'I', 
    'n',  'c',  'r',  'e',  'm',  'e',  'n',  't', 
    'a',  'l',  'S',  'e',  'a',  'r',  'c',  'h', 
    'P',  'l',  'u',  'g',  'i',  'n', 
    // "className"
    0x03,  0x77,  'I',  'n',  'c',  'r',  'e',  'm', 
    'e',  'n',  't',  'a',  'l',  'S',  'e',  'a', 
    'r',  'c',  'h',  'P',  'l',  'u',  'g',  'i', 
    'n', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN_V2(IncrementalSearchPlugin, IncrementalSearchPlugin, qt_pluginMetaDataV2_IncrementalSearchPlugin)
#else
QT_PLUGIN_METADATA_SECTION
Q_CONSTINIT static constexpr unsigned char qt_pluginMetaData_IncrementalSearchPlugin[] = {
    'Q', 'T', 'M', 'E', 'T', 'A', 'D', 'A', 'T', 'A', ' ', '!',
    // metadata version, Qt version, architectural requirements
    0, QT_VERSION_MAJOR, QT_VERSION_MINOR, qPluginArchRequirements(),
    0xbf, 
    // "IID"
    0x02,  0x78,  0x23,  's',  'o',  'r',  'a',  'm', 
    'i',  'm',  'i',  '.',  'j',  'p',  '.',  'I', 
    'n',  'c',  'r',  'e',  'm',  'e',  'n',  't', 
    'a',  'l',  'S',  'e',  'a',  'r',  'c',  'h', 
    'P',  'l',  'u',  'g',  'i',  'n', 
    // "className"
    0x03,  0x77,  'I',  'n',  'c',  'r',  'e',  'm', 
    'e',  'n',  't',  'a',  'l',  'S',  'e',  'a', 
    'r',  'c',  'h',  'P',  'l',  'u',  'g',  'i', 
    'n', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN(IncrementalSearchPlugin, IncrementalSearchPlugin)
#endif  // QT_MOC_EXPORT_PLUGIN_V2

QT_WARNING_POP
