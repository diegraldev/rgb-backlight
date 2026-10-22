#include "rgbdevicecontrol.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QFileInfo>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>

namespace
{
const QString KeyboardDevice = QStringLiteral("keyboard");
const QString MouseDevice = QStringLiteral("mouse");

const QString KeyboardLed = QStringLiteral("/sys/class/leds/msi::kbd_backlight");
const QString MouseLed = QStringLiteral("/sys/class/leds/razer::logo_backlight");

const QString UPowerService = QStringLiteral("org.freedesktop.UPower");
const QString UPowerPath = QStringLiteral("/org/freedesktop/UPower/KbdBacklight");
const QString UPowerInterface = QStringLiteral("org.freedesktop.UPower.KbdBacklight");
}

RgbDeviceControl::RgbDeviceControl(QObject *parent)
    : QObject(parent)
{
    // El dispositivo se indica con la propiedad "device":
    //   RgbDeviceControl { device: "mouse" }
    // Se deja vacio a proposito: al asignar "device" siempre se relee
    // el estado real del dispositivo.
    m_device = QString();
}

QString RgbDeviceControl::device() const
{
    return m_device;
}

void RgbDeviceControl::setDevice(const QString &device)
{
    if (device != m_device) {
        m_device = device;
        Q_EMIT deviceChanged();
    }

    refresh();
}

QString RgbDeviceControl::ledPath() const
{
    return m_device == MouseDevice ? MouseLed : KeyboardLed;
}

bool RgbDeviceControl::readValue(const QString &attribute, QString *value) const
{
    QFile file(ledPath() + QLatin1Char('/') + attribute);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    *value = QString::fromUtf8(file.readAll().trimmed());
    return true;
}

bool RgbDeviceControl::writeValue(const QString &attribute, const QString &value) const
{
    QFile file(ledPath() + QLatin1Char('/') + attribute);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    const QByteArray data = value.toUtf8();
    return file.write(data) == data.size();
}

void RgbDeviceControl::refresh()
{
    const bool available = QFileInfo::exists(ledPath() + QStringLiteral("/brightness"));
    if (available != m_available) {
        m_available = available;
        Q_EMIT availableChanged();
    }

    if (!m_available)
        return;

    QString value;
    if (readValue(QStringLiteral("max_brightness"), &value))
        m_maxBrightness = qMax(1, value.toInt());

    if (readValue(QStringLiteral("brightness"), &value)) {
        m_brightness = value.toInt() * 100 / m_maxBrightness;
        Q_EMIT brightnessChanged();
    }

    if (m_device == MouseDevice) {
        if (readValue(QStringLiteral("color"), &value)) {
            m_color = QColor(QStringLiteral("#") + value);
            Q_EMIT colorChanged();
        }
        if (readValue(QStringLiteral("color2"), &value)) {
            m_secondaryColor = QColor(QStringLiteral("#") + value);
            Q_EMIT colorChanged();
        }
        if (readValue(QStringLiteral("effect"), &value)) {
            m_effect = value.toInt();
            Q_EMIT effectChanged();
        }
        if (readValue(QStringLiteral("speed"), &value)) {
            m_speed = value.toInt();
            Q_EMIT speedChanged();
        }
    } else {
        if (readValue(QStringLiteral("colors"), &value)) {
            const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (!parts.isEmpty())
                m_color = QColor(QStringLiteral("#") + parts.first());
            if (parts.size() > 1)
                m_secondaryColor = QColor(QStringLiteral("#") + parts.at(1));
            Q_EMIT colorChanged();
        }
        if (readValue(QStringLiteral("mode"), &value)) {
            m_effect = value.toInt();
            Q_EMIT effectChanged();
        }
        if (readValue(QStringLiteral("speed"), &value)) {
            m_speed = value.toInt();
            Q_EMIT speedChanged();
        }
    }
}

bool RgbDeviceControl::available() const
{
    return m_available;
}

int RgbDeviceControl::brightness() const
{
    return m_brightness;
}

void RgbDeviceControl::setBrightness(int brightness)
{
    if (!m_available || brightness < 0)
        return;

    brightness = qBound(0, brightness, 100);
    if (brightness == m_brightness)
        return;

    if (m_device == MouseDevice) {
        if (!writeValue(QStringLiteral("brightness"), QString::number(brightness)))
            return;
        m_brightness = brightness;
        Q_EMIT brightnessChanged();
        return;
    }

    /* El teclado lo controla UPower para no pelearnos con PowerDevil */
    setBrightnessViaUPower(brightness);
}

void RgbDeviceControl::setBrightnessViaUPower(int brightness)
{
    QDBusInterface iface(UPowerService, UPowerPath, UPowerInterface,
                         QDBusConnection::systemBus());
    if (!iface.isValid())
        return;

    const int value = qRound(brightness * m_maxBrightness / 100.0);
    const QDBusReply<void> reply = iface.call(QStringLiteral("SetBrightness"), value);
    if (!reply.isValid())
        return;

    m_brightness = brightness;
    Q_EMIT brightnessChanged();
}

QColor RgbDeviceControl::color() const
{
    return m_color;
}

void RgbDeviceControl::setColor(const QColor &color)
{
    if (!m_available || !color.isValid() || color == m_color)
        return;

    if (m_device == MouseDevice) {
        if (!writeValue(QStringLiteral("color"), color.name(QColor::HexRgb).mid(1)))
            return;
    } else {
        /* OJO: hay que escribir el color NUEVO, no el anterior. */
        const QString colors = color.name(QColor::HexRgb).mid(1) + QLatin1Char(',')
                + m_secondaryColor.name(QColor::HexRgb).mid(1);
        if (!writeValue(QStringLiteral("colors"), colors))
            return;
    }

    m_color = color;
    Q_EMIT colorChanged();
}

bool RgbDeviceControl::hasSecondaryColor() const
{
    return true;
}

QColor RgbDeviceControl::secondaryColor() const
{
    return m_secondaryColor;
}

void RgbDeviceControl::setSecondaryColor(const QColor &color)
{
    if (!m_available || !color.isValid() || color == m_secondaryColor)
        return;

    if (m_device == MouseDevice) {
        if (!writeValue(QStringLiteral("color2"), color.name(QColor::HexRgb).mid(1)))
            return;
    } else {
        const QString colors = m_color.name(QColor::HexRgb).mid(1) + QLatin1Char(',')
                + color.name(QColor::HexRgb).mid(1);
        if (!writeValue(QStringLiteral("colors"), colors))
            return;
    }

    m_secondaryColor = color;
    Q_EMIT colorChanged();
}

QStringList RgbDeviceControl::effects() const
{
    if (m_device == MouseDevice) {
        /* Orden identico al del modulo del kernel (rgb-backlight.c) */
        return {
            QObject::tr("Off"),
            QObject::tr("Static"),
            QObject::tr("Spectrum cycle"),
            QObject::tr("Reactive"),
            QObject::tr("Breathing random"),
            QObject::tr("Breathing single"),
            QObject::tr("Breathing dual")
        };
    }

    return {
        QObject::tr("Off"),
        QObject::tr("Static"),
        QObject::tr("Breathing"),
        QObject::tr("Flashing"),
        QObject::tr("Double flashing"),
        QObject::tr("Color cycle"),
        QObject::tr("Smooth color cycle")
    };
}

int RgbDeviceControl::effect() const
{
    return m_effect;
}

void RgbDeviceControl::setEffect(int effect)
{
    if (!m_available || effect < 0 || effect >= effects().size() || effect == m_effect)
        return;

    const QString attribute = (m_device == MouseDevice) ? QStringLiteral("effect")
                                                        : QStringLiteral("mode");
    if (!writeValue(attribute, QString::number(effect)))
        return;

    m_effect = effect;
    Q_EMIT effectChanged();
}

bool RgbDeviceControl::hasSpeed() const
{
    return true;
}

int RgbDeviceControl::speed() const
{
    return m_speed;
}

void RgbDeviceControl::setSpeed(int speed)
{
    if (!m_available || speed < 0 || speed > 2 || speed == m_speed)
        return;

    if (m_device == MouseDevice) {
        if (!writeValue(QStringLiteral("speed"), QString::number(speed)))
            return;
    } else if (!writeValue(QStringLiteral("speed"), QString::number(speed))) {
        return;
    }

    m_speed = speed;
    Q_EMIT speedChanged();
}

void RgbBacklightPlugin::registerTypes(const char *uri)
{
    Q_ASSERT(QLatin1String(uri) == QLatin1String("org.rgb.backlight"));
    qmlRegisterType<RgbDeviceControl>("org.rgb.backlight", 1, 0, "RgbDeviceControl");
}

#include "moc_rgbdevicecontrol.cpp"
