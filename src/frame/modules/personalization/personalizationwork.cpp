// SPDX-FileCopyrightText: 2011 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "personalizationwork.h"
#include "model/thememodel.h"
#include "model/fontmodel.h"
#include "model/fontsizemodel.h"
#include "window/dconfigwatcher.h"
#include "window/utils.h"

#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

using namespace dcc;
using namespace dcc::personalization;

#define GSETTING_EFFECT_LOAD "effect-load"

const QString Service = "com.deepin.daemon.Appearance";
const QString Path    = "/com/deepin/daemon/Appearance";
const QString EffectMoveWindowArg = "kwin4_effect_translucency";
const QString Scale = "kwin4_effect_scale";
const QString Magiclamp = "magiclamp";
const QString IsEffectSupported = "isEffectSupported";
const QString StrIsOpenWM = "deepin wm";

static const std::vector<int> OPACITY_SLIDER {
    0,
    25,
    40,
    55,
    70,
    85,
    100
};

PersonalizationWork::PersonalizationWork(PersonalizationModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_dbus(new Appearance(Service, Path, QDBusConnection::sessionBus(), this))
    , m_interface(new QDBusInterface(Service, Path, Service, QDBusConnection::sessionBus()))
    , m_wmSwitcher(new WMSwitcher("com.deepin.WMSwitcher", "/com/deepin/WMSwitcher", QDBusConnection::sessionBus(), this))
    , m_wm(new WM("com.deepin.wm", "/com/deepin/wm", QDBusConnection::sessionBus(), this))
    , m_effects(new Effects("org.kde.KWin", "/Effects", QDBusConnection::sessionBus(), this))
    , m_isWayland(qEnvironmentVariable("XDG_SESSION_TYPE").contains("wayland"))
{
    ThemeModel *cursorTheme      = m_model->getMouseModel();
    ThemeModel *windowTheme      = m_model->getWindowModel();
    ThemeModel *iconTheme        = m_model->getIconModel();
    FontModel *fontMono          = m_model->getMonoFontModel();
    FontModel *fontStand         = m_model->getStandFontModel();
    m_setting = new QGSettings("com.deepin.dde.control-center", QByteArray(), this);

    connect(m_dbus, &Appearance::GtkThemeChanged,      windowTheme,   &ThemeModel::setDefault);
    connect(m_dbus, &Appearance::CursorThemeChanged,   cursorTheme,   &ThemeModel::setDefault);
    connect(m_dbus, &Appearance::IconThemeChanged,     iconTheme,     &ThemeModel::setDefault);
    connect(m_dbus, &Appearance::MonospaceFontChanged, fontMono,      &FontModel::setFontName);
    connect(m_dbus, &Appearance::StandardFontChanged,  fontStand,     &FontModel::setFontName);
    connect(m_dbus, &Appearance::FontSizeChanged, this, &PersonalizationWork::FontSizeChanged);
    connect(m_dbus, &Appearance::Refreshed, this, &PersonalizationWork::onRefreshedChanged);

    //connect(m_wmSwitcher, &WMSwitcher::WMChanged, this, &PersonalizationWork::onToggleWM);
    connect(m_dbus, &Appearance::OpacityChanged, this, &PersonalizationWork::refreshOpacity);
    connect(m_dbus, &Appearance::QtActiveColorChanged, this, &PersonalizationWork::refreshActiveColor);
    connect(m_wm, &WM::CompositingAllowSwitchChanged, this, &PersonalizationWork::onCompositingAllowSwitch);
    connect(m_wm, &WM::compositingEnabledChanged, this, &PersonalizationWork::onWindowWM);

    // 监听窗口圆角值变化信号，以及后续增加的其他属性值变化均可在此监听
    QDBusConnection::sessionBus().connect(Service, Path,
                                          "org.freedesktop.DBus.Properties",
                                          "PropertiesChanged",
                                          "sa{sv}as",
                                          this,
                                          SLOT(handlePropertiesChanged(QDBusMessage)));

    //获取最小化设置
    if (m_setting->keys().contains("effectLoad", Qt::CaseSensitivity::CaseInsensitive)) {
        bool isMinEffect = m_setting->get(GSETTING_EFFECT_LOAD).toBool();
        m_model->setMiniEffect(isMinEffect);
        if (isMinEffect) {
            m_effects->loadEffect(Magiclamp);
        } else {
            m_effects->unloadEffect(Magiclamp);
        }
    } else {
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(m_effects->isEffectLoaded(Magiclamp), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [ = ] (QDBusPendingCallWatcher *watcher) {
            if (!watcher->isError()) {
                QDBusReply<bool> value = watcher->reply();
                if (value) {
                    m_model->setMiniEffect(1);
                } else {
                    m_model->setMiniEffect(0);
                }
            } else {
                qWarning() << watcher->error();
            }
        });
    };

    m_themeModels["gtk"]           = windowTheme;
    m_themeModels["icon"]          = iconTheme;
    m_themeModels["cursor"]        = cursorTheme;
    m_fontModels["standardfont"]   = fontStand;
    m_fontModels["monospacefont"]  = fontMono;

    m_dbus->setSync(false);
    m_wmSwitcher->setSync(false);
}

void PersonalizationWork::refreshEffectModule()
{
    QDBusInterface effects("org.kde.KWin", "/Effects", "org.kde.kwin.Effects", QDBusConnection::sessionBus(), this);
    if (!effects.isValid()) {
        qWarning() << " The interface of org.kde.kwin.Effects is invalid.";
        return;
    }

    // 虚拟机同步调用接口后，不会有返回
    // 需要先使用同步接口，在获取结果为false时，再使用异步接口
    QDBusReply<bool> isScaleSupported = effects.call(IsEffectSupported, Scale);
    m_model->setIsEffectSupportScale(isScaleSupported);
    qInfo() << " [refreshEffectModule] effects.call(IsEffectSupported, Scale) : " << isScaleSupported.value();

    QDBusReply<bool> isMagicSupported = effects.call(IsEffectSupported, Magiclamp);
    m_model->setIsEffectSupportMagiclamp(isMagicSupported);
    qInfo() << " [refreshEffectModule] effects.call(IsEffectSupported, Magiclamp) : " << isMagicSupported.value();

    QDBusReply<bool> isMoveWinSupported = effects.call(IsEffectSupported, EffectMoveWindowArg);
    m_model->setIsEffectSupportMoveWindow(isMoveWinSupported);
    qInfo() << " [refreshEffectModule] effects.call(IsEffectSupported, EffectMoveWindowArg) : " << isMoveWinSupported.value();

    // 当同步获取到数据为false，可能是kwin接口存在问题再用异步方式获取一次
    if (!isScaleSupported) {
        // 最小化时效果 : 缩放
        QDBusPendingCall scaleEffectReply = effects.asyncCall(IsEffectSupported, Scale);
        QDBusPendingCallWatcher *scaleEffectWatcher = new QDBusPendingCallWatcher(scaleEffectReply, this);
        connect(scaleEffectWatcher, &QDBusPendingCallWatcher::finished, [this, scaleEffectReply, scaleEffectWatcher] {
            scaleEffectWatcher->deleteLater();

            if (scaleEffectReply.isError()) {
                qWarning() << " isEffectSupported Failed to get kwin4_effect_scale state: " << scaleEffectReply.error().message();
                return;
            }

            QDBusReply<bool> reply = scaleEffectReply.reply();
            if (reply.value() && m_model) {
                m_model->setIsEffectSupportScale(true);
            }
        });
    }

    if (!isMagicSupported) {
        // 最小化时效果 : 魔灯
        QDBusPendingCall magiclampEffectReply = effects.asyncCall(IsEffectSupported, Magiclamp);
        QDBusPendingCallWatcher *magiclampEffectWatcher = new QDBusPendingCallWatcher(magiclampEffectReply, this);
        connect(magiclampEffectWatcher, &QDBusPendingCallWatcher::finished, [this, magiclampEffectReply, magiclampEffectWatcher] {
            magiclampEffectWatcher->deleteLater();

            if (magiclampEffectReply.isError()) {
                qWarning() << " isEffectSupported Failed to get magiclamp state: " << magiclampEffectReply.error().message();
                return;
            }

            QDBusReply<bool> reply = magiclampEffectReply.reply();
            if (reply.value() && m_model) {
                m_model->setIsEffectSupportMagiclamp(true);
            }
        });
    }

    if (!isMoveWinSupported) {
        // 移动窗口时启动特效
        QDBusPendingCall translucencyEffectReply = effects.asyncCall(IsEffectSupported, EffectMoveWindowArg);
        QDBusPendingCallWatcher *translucencyEffectWatcher = new QDBusPendingCallWatcher(translucencyEffectReply, this);
        connect(translucencyEffectWatcher, &QDBusPendingCallWatcher::finished, [this, translucencyEffectReply, translucencyEffectWatcher] {
            translucencyEffectWatcher->deleteLater();

            if (translucencyEffectReply.isError()) {
                qWarning() << " isEffectSupported Failed to get kwin4_effect_translucency state: " << translucencyEffectReply.error().message();
                return;
            }

            QDBusReply<bool> reply = translucencyEffectReply.reply();
            if (reply.value() && m_model) {
                m_model->setIsEffectSupportMoveWindow(true);
            }
        });
    }
}

void PersonalizationWork::setScrollBarPolicy(int policy)
{
    if (m_interface->isValid()) {
        m_interface->setProperty("QtScrollBarPolicy", policy);
    }
}

void PersonalizationWork::setCompactDisplay(bool enabled)
{
    if (m_interface->isValid()) {
        m_interface->setProperty("DTKSizeMode", int(enabled));
    }
}

void PersonalizationWork::active()
{
    m_dbus->blockSignals(false);
    m_wmSwitcher->blockSignals(false);

    refreshWMState();
    refreshOpacity(m_dbus->opacity());
    refreshActiveColor(m_dbus->qtActiveColor());
    onCompositingAllowSwitch(m_wm->compositingAllowSwitch());

    m_model->getWindowModel()->setDefault(m_dbus->gtkTheme());
    m_model->getIconModel()->setDefault(m_dbus->iconTheme());
    m_model->getMouseModel()->setDefault(m_dbus->cursorTheme());
    m_model->getMonoFontModel()->setFontName(m_dbus->monospaceFont());
    m_model->getStandFontModel()->setFontName(m_dbus->standardFont());

    if (m_interface->isValid()) {
        bool ok = false;
        int radius = m_interface->property("WindowRadius").toInt(&ok);
        if (ok)
            m_model->setWindowRadius(radius);

        int policy = m_interface->property("QtScrollBarPolicy").toInt(&ok);
        m_model->setScrollBarPolicy(ok ? policy : PersonalizationModel::ShowOnScrolling);
    }

}

void PersonalizationWork::deactive()
{
    m_dbus->blockSignals(true);
    m_wmSwitcher->blockSignals(true);
}

QList<QJsonObject> PersonalizationWork::converToList(const QString &type, const QJsonArray &array)
{
    QList<QJsonObject> list;
    for (int i = 0; i != array.size(); i++) {
        QJsonObject object = array.at(i).toObject();
        object.insert("type", QJsonValue(type));
        list.append(object);
    }
    return list;
}

void PersonalizationWork::addList(ThemeModel *model, const QString &type, const QJsonArray &array)
{
    QList<QString> list;
    QList<QJsonObject> objList;
    for (int i = 0; i != array.size(); i++) {
        QJsonObject object = array.at(i).toObject();
        object.insert("type", QJsonValue(type));
        objList << object;
        list.append(object["Id"].toString());

        QDBusPendingReply<QString> pic = m_dbus->Thumbnail(type, object["Id"].toString());
        QDBusPendingCallWatcher *picWatcher = new QDBusPendingCallWatcher(pic, this);
        picWatcher->setProperty("category", type);
        picWatcher->setProperty("id", object["Id"].toString());
        connect(picWatcher, &QDBusPendingCallWatcher::finished, this, &PersonalizationWork::onGetPicFinished);
    }

    // sort for display name
    std::sort(objList.begin(), objList.end(), [=] (const QJsonObject &obj1, const QJsonObject &obj2) {
        QCollator qc;
        return qc.compare(obj1["Id"].toString(), obj2["Id"].toString()) < 0;
    });

    for (const QJsonObject &obj : objList) {
        model->addItem(obj["Id"].toString(), obj);
    }

    for (const QString &id : model->getList().keys()) {
        if (!list.contains(id)) {
            model->removeItem(id);
        }
    }
}

void PersonalizationWork::refreshWMState()
{
    // wayland默认支持且开启特效
    if (m_isWayland) {
        refreshMoveWindowState();
    } else {
        QDBusPendingCallWatcher *wmWatcher = new QDBusPendingCallWatcher(m_wmSwitcher->CurrentWM(), this);
        connect(wmWatcher, &QDBusPendingCallWatcher::finished, this, &PersonalizationWork::onGetCurrentWMFinished);
    }
}

void PersonalizationWork::refreshMoveWindowState()
{
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(m_effects->isEffectLoaded(EffectMoveWindowArg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [ = ] (QDBusPendingCallWatcher *watcher) {
        if (!watcher->isError()) {
            QDBusReply<bool> isMoveWindow = watcher->reply();
            qDebug() << Q_FUNC_INFO << isMoveWindow;
            m_model->setIsMoveWindow(isMoveWindow);
            m_model->setIsMoveWindowDconfig(isMoveWindow);
        } else {
            qWarning() << "[refreshMoveWindowState] isEffectLoaded err : " << watcher->error();
        }
        watcher->deleteLater();
    });
}

void PersonalizationWork::FontSizeChanged(const double value) const
{
    FontSizeModel *fontSizeModel = m_model->getFontSizeModel();
    fontSizeModel->setFontSize(sizeToSliderValue(value));
}

void PersonalizationWork::onGetFontFinished(QDBusPendingCallWatcher *w)
{
    QDBusPendingReply<QString> reply = *w;

    if (!reply.isError()) {
        const QString &category = w->property("category").toString();

        setFontList(m_fontModels[category], category, reply.value());
    } else {
        qWarning() << reply.error();
    }

    w->deleteLater();
}

void PersonalizationWork::onGetThemeFinished(QDBusPendingCallWatcher *w)
{
    QDBusPendingReply<QString> reply = *w;

    if (!reply.isError()) {
        const QString &category = w->property("category").toString();
        const QJsonArray &array = QJsonDocument::fromJson(reply.value().toUtf8()).array();

        addList(m_themeModels[category], category, array);
    } else {
        qWarning() << reply.error();
    }

    w->deleteLater();
}

void PersonalizationWork::onGetPicFinished(QDBusPendingCallWatcher *w)
{
    QDBusPendingReply<QString> reply = *w;

    if (!reply.isError()) {
        const QString &category = w->property("category").toString();
        const QString &id = w->property("id").toString();

        m_themeModels[category]->addPic(id, reply.value());
    } else {
        qWarning() << reply.error();
    }

    w->deleteLater();
}

void PersonalizationWork::onGetActiveColorFinished(QDBusPendingCallWatcher *w)
{
    QDBusPendingReply<QString> reply = *w;

    if (!reply.isError()) {
        m_model->setActiveColor(reply.value());
    } else {
        qWarning() << reply.error();
    }

    w->deleteLater();
}

void PersonalizationWork::onRefreshedChanged(const QString &type)
{
    if (m_themeModels.keys().contains(type)) {
        refreshThemeByType(type);
    }

    if (m_fontModels.keys().contains(type)) {
        refreshFontByType(type);
    }
}

void PersonalizationWork::onToggleWM(const QString &wm)
{
    bool is3D = wm == StrIsOpenWM;
    qDebug() << "onToggleWM: " << wm << is3D;
    m_model->setIs3DWm(is3D);
    if (is3D) {
        refreshMoveWindowState();
    }
}

void PersonalizationWork::setMoveWindow(bool state)
{
    if (!m_effects) {
        qWarning() << "The Interface of org::kde::kwin::Effects is nullptr.";
        return;
    }

    if (!m_model) {
        return;
    }

    if (state) {
        m_model->setIsMoveWindow(m_model->getIsMoveWindowDconfig());
    } else {
        m_model->setIsMoveWindowDconfig(m_model->isMoveWindow());
        m_model->setIsMoveWindow(false);
    }
}

void PersonalizationWork::onWindowWM(bool value)
{
    qDebug() << "onWindowWM: " << value;
    m_model->setIs3DWm(value);
    setMoveWindow(value);
}

void PersonalizationWork::onCompositingAllowSwitch(bool value)
{
    m_model->setCompositingAllowSwitch(value);
}

void PersonalizationWork::onGetCurrentWMFinished(QDBusPendingCallWatcher *w)
{
    QDBusPendingReply<QString> reply = w->reply();

    if (!reply.isError()) {
        onToggleWM(reply.value());
    } else {
        qDebug() << reply.error();
    }

    w->deleteLater();
}

void PersonalizationWork::setFontList(FontModel *model, const QString &type, const QString &list)
{
    QJsonArray array = QJsonDocument::fromJson(list.toLocal8Bit().data()).array();

    QStringList l;

    for (int i = 0; i != array.size(); i++)
        l << array.at(i).toString();

    QDBusPendingCallWatcher *watcher  = new QDBusPendingCallWatcher(m_dbus->Show(type, l), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [=] (QDBusPendingCallWatcher *w) {
        if (!w->isError()) {
            QDBusPendingReply<QString> r = w->reply();

            QJsonArray arrayValue = QJsonDocument::fromJson(r.value().toLocal8Bit().data()).array();

            QList<QJsonObject> list = converToList(type, arrayValue);
            // sort for display name
            std::sort(list.begin(), list.end(), [=] (const QJsonObject &obj1, const QJsonObject &obj2) {
                QCollator qc;
                return qc.compare(obj1["Name"].toString(), obj2["Name"].toString()) < 0;
            });

            model->setFontList(list);
        } else {
            qDebug() << w->error();
        }

        watcher->deleteLater();
    });
}

void PersonalizationWork::refreshTheme()
{
    for (QMap<QString, ThemeModel *>::ConstIterator it = m_themeModels.begin(); it != m_themeModels.end(); it++) {
        refreshThemeByType(it.key());
    }
}

void PersonalizationWork::refreshThemeByType(const QString &type)
{
    QDBusPendingReply<QString> theme = m_dbus->List(type);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(theme, this);
    watcher->setProperty("category", type);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &PersonalizationWork::onGetThemeFinished);
}

void PersonalizationWork::refreshFont()
{
    for (QMap<QString, FontModel *>::const_iterator it = m_fontModels.begin(); it != m_fontModels.end(); it++) {
        refreshFontByType(it.key());
    }
    m_model->setCompactDisplay(m_interface->isValid() ? m_interface->property("DTKSizeMode").toBool() : false);
    FontSizeChanged(m_dbus->fontSize());
}

void PersonalizationWork::refreshFontByType(const QString &type)
{
    QDBusPendingReply<QString> font = m_dbus->List(type);
    QDBusPendingCallWatcher *fontWatcher = new QDBusPendingCallWatcher(font, this);
    fontWatcher->setProperty("category", type);
    connect(fontWatcher, &QDBusPendingCallWatcher::finished, this, &PersonalizationWork::onGetFontFinished);
}

void PersonalizationWork::refreshActiveColor(const QString &color)
{
    m_model->setActiveColor(color);
}

bool PersonalizationWork::allowSwitchWM()
{
    QDBusPendingReply<bool> reply  = m_wmSwitcher->AllowSwitch();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [=](QDBusPendingCallWatcher *w) {
        if (w->isError())
            qDebug() << watcher->error();
        watcher->deleteLater();
    });
    return reply.value();
}

void PersonalizationWork::refreshOpacity(double opacity)
{
#ifdef WINDOW_MODE
    int slider { static_cast<int>(opacity * 100) };
#else
    int slider { toSliderValue<int>(OPACITY_SLIDER, static_cast<int>(opacity * 100)) };
#endif
    qDebug() << QString("opacity: %1, slider: %2").arg(opacity).arg(slider);
    m_model->setOpacity(std::pair<int, double>(slider, opacity));
}

#ifdef WINDOW_MODE
const int RENDER_DPI = 72;
const double DPI = 96;

double ptToPx(double pt) {
    double px = pt / RENDER_DPI * DPI + 0.5;
    return px;
}

double pxToPt(double px) {
    double pt = px * RENDER_DPI / DPI;
    return pt;
}

//字体大小通过点击刻度调整字体大小，可选刻度为：11px、12px、13px、14px、15px、16px、18px、20px;
//社区版默认值为12px；专业版默认值为12px；
int PersonalizationWork::sizeToSliderValue(const double value) const
{
    int px = static_cast<int>(ptToPx(value));

    QList<int> sizeList = DCC_NAMESPACE::FontSizeList;
    if (m_model->compactDisplay()) {
        sizeList = DCC_NAMESPACE::FontSizeList_Compact;
    }

    if (px < sizeList.first()) {
        return 0;
    } else if (px > sizeList.last()){
        return (sizeList.size() - 1);
    }

    return sizeList.indexOf(px);
}

double PersonalizationWork::sliderValueToSize(const int value) const
{
    QList<int> sizeList = DCC_NAMESPACE::FontSizeList;
    if (m_model->compactDisplay()) {
        sizeList = DCC_NAMESPACE::FontSizeList_Compact;
    }
    return pxToPt(sizeList.at(value));
}
#else
int PersonalizationWork::sizeToSliderValue(const double value) const
{
    if (value <= 8.2) {
        return 0;
    } else if (value <= 9) {
        return 1;
    } else if (value <= 9.7) {
        return 2;
    } else if (value <= 11.2) {
        return 3;
    } else if (value <= 12) {
        return 4;
    } else if (value <= 13.5) {
        return 5;
    } else if (value >= 14.5) {
        return 6;
    } else {
        return 1;
    }
}

double PersonalizationWork::sliderValueToSize(const int value) const
{
    switch (value) {
    case 0:
        return 8.2;
    case 1:
        return 9;
    case 2:
        return 9.7;
    case 3:
        return 11.2;
    case 4:
        return 12;
    case 5:
        return 13.5;
    case 6:
        return 14.5;
    default:
        return 9;
    }
}
#endif

double PersonalizationWork::sliderValutToOpacity(const int value) const
{
#ifdef WINDOW_MODE
    return static_cast<double>(value) / static_cast<double>(100);
#else
    return static_cast<double>(OPACITY_SLIDER[value]) / static_cast<double>(100);
#endif
}

void PersonalizationWork::setDefault(const QJsonObject &value)
{
    //使用type去调用
    m_dbus->Set(value["type"].toString(), value["Id"].toString());
}

void PersonalizationWork::setFontSize(const int value)
{
    m_dbus->setFontSize(sliderValueToSize(value));
}

void PersonalizationWork::switchWM()
{
    //check is allowed to switch wm
    bool allow = allowSwitchWM();
    if (!allow)
        return;

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(m_wmSwitcher->RequestSwitchWM(), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [=] {
        if (watcher->isError()) {
            qDebug() << watcher->error();
        }
        watcher->deleteLater();
    });
}

void PersonalizationWork::windowSwitchWM(bool value)
{
    QDBusInterface Interface("com.deepin.wm",
                                 "/com/deepin/wm",
                                "org.freedesktop.DBus.Properties",
                                QDBusConnection::sessionBus());

     QDBusMessage reply = Interface.call("Set","com.deepin.wm","compositingEnabled", QVariant::fromValue(QDBusVariant(value)));
     if (reply.type() == QDBusMessage::ErrorMessage) {
         qDebug() << "reply.type() = " << reply.type();
     }

}

void PersonalizationWork::movedWindowSwitchWM(bool value)
{
    if (!m_effects) {
        qWarning() << "The Interface of org::kde::kwin::Effects is nullptr.";
        return;
    }

    if (value) {
        m_effects->loadEffect(EffectMoveWindowArg);
    } else {
        m_effects->unloadEffect(EffectMoveWindowArg);
    }

    //设置kwin接口后, 等待50ms给kwin反应，根据isEffectLoaded调用的异步返回值确定真实状态
    QTimer::singleShot(50, [this] {
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(m_effects->isEffectLoaded(EffectMoveWindowArg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [ = ] (QDBusPendingCallWatcher *watcher) {
            if (!watcher->isError()) {
                QDBusReply<bool> value = watcher->reply();
                qDebug() << Q_FUNC_INFO << " isEffectLoaded('kwin4_effect_translucency') : " << value;
                m_model->setIsMoveWindow(value);
                m_model->setIsMoveWindowDconfig(value);
            } else {
                qWarning() << Q_FUNC_INFO << " isEffectLoaded('kwin4_effect_translucency'), error : " << watcher->error();;
            }
        });
    });
}

void PersonalizationWork::setOpacity(int opacity)
{
    m_dbus->setOpacity(sliderValutToOpacity(opacity));
}

void PersonalizationWork::setMiniEffect(int effect)
{
    switch(effect){
    case 0:
        qDebug() << "scale";
        m_effects->unloadEffect("magiclamp");
        m_setting->set(GSETTING_EFFECT_LOAD, false);
        m_model->setMiniEffect(effect);
        break;
    case 1:
        qDebug() << "magiclamp";
        m_effects->loadEffect("magiclamp");
        m_setting->set(GSETTING_EFFECT_LOAD, true);
        m_model->setMiniEffect(effect);
        break;
    default:break;
    }
}

void PersonalizationWork::setActiveColor(const QString &hexColor)
{
    m_dbus->setQtActiveColor(hexColor);
}

void PersonalizationWork::setWindowRadius(int radius)
{
    if (m_interface->isValid()) {
        m_interface->setProperty("WindowRadius", radius);
    }
}

void PersonalizationWork::handlePropertiesChanged(QDBusMessage msg)
{
    QList<QVariant> arguments = msg.arguments();
    if (3 != arguments.count()) {
        return;
    }
    QString interfaceName = msg.arguments().at(0).toString();
    if (interfaceName == Service) {
        QVariantMap changedProps = qdbus_cast<QVariantMap>(arguments.at(1).value<QDBusArgument>());
        QStringList keys = changedProps.keys();
        for (int i = 0; i < keys.size(); i++) {
            // 监听窗口圆角值信号
            if (keys.at(i) == "WindowRadius") {
                int radius = static_cast<int>(changedProps.value(keys.at(i)).toInt());
                m_model->setWindowRadius(radius);
            } else if (keys.at(i) == "QtScrollBarPolicy") {
                int policy = static_cast<int>(changedProps.value(keys.at(i)).toInt());
                m_model->setScrollBarPolicy(policy);
            } else if (keys.at(i) == "DTKSizeMode") {
                int enabled = static_cast<int>(changedProps.value(keys.at(i)).toBool());
                m_model->setCompactDisplay(enabled);
            }
        }
    }
}

template<typename T>
T PersonalizationWork::toSliderValue(std::vector<T> list, T value)
{
    for (auto it = list.cbegin(); it != list.cend(); ++it) {
        if (value < *it) {
            return (--it) - list.begin();
        }
    }

    return list.end() - list.begin();
}
