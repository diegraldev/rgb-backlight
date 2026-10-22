/*
 * RgbDeviceControl - tipo QML para controlar la retroiluminacion RGB de
 * dispositivos USB (teclado MSI Mystic Light, raton Razer Viper Mini).
 *
 * Todo se escribe en el modulo del kernel (rgb-backlight), que es el unico
 * que habla con el hardware, de modo que el estado no puede quedar
 * desincronizado.  El brillo del teclado se escribe a traves de UPower
 * (org.freedesktop.UPower.KbdBacklight) para no pelearnos con PowerDevil.
 */
#pragma once

#include <QColor>
#include <QObject>
#include <QQmlExtensionPlugin>
#include <QStringList>

class QTimer;

class RgbDeviceControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool hasSecondaryColor READ hasSecondaryColor CONSTANT)
    Q_PROPERTY(QColor secondaryColor READ secondaryColor WRITE setSecondaryColor NOTIFY colorChanged)
    Q_PROPERTY(QStringList effects READ effects CONSTANT)
    Q_PROPERTY(int effect READ effect WRITE setEffect NOTIFY effectChanged)
    Q_PROPERTY(bool hasSpeed READ hasSpeed CONSTANT)
    Q_PROPERTY(int speed READ speed WRITE setSpeed NOTIFY speedChanged)

public:
    explicit RgbDeviceControl(QObject *parent = nullptr);

    QString device() const;
    void setDevice(const QString &device);
    bool available() const;
    int brightness() const;
    void setBrightness(int brightness);
    QColor color() const;
    void setColor(const QColor &color);
    bool hasSecondaryColor() const;
    QColor secondaryColor() const;
    void setSecondaryColor(const QColor &color);
    QStringList effects() const;
    int effect() const;
    void setEffect(int effect);
    bool hasSpeed() const;
    int speed() const;
    void setSpeed(int speed);

    /** Re-lee el estado real del dispositivo. */
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void deviceChanged();
    void availableChanged();
    void brightnessChanged();
    void colorChanged();
    void effectChanged();
    void speedChanged();

private:
    QString ledPath() const;
    bool readValue(const QString &attribute, QString *value) const;
    bool writeValue(const QString &attribute, const QString &value) const;
    void setBrightnessViaUPower(int brightness);

    /*
     * Perfil del dispositivo.  Cada cambio que se escribe en el modulo se
     * guarda en ~/.config/rgb-backlight/<dispositivo>.state, de donde el
     * servicio de sesion lo recupera al iniciar sesion.  Las escrituras se
     * agrupan con un temporizador (al arrastrar un deslizador se generan
     * decenas de cambios) para no tocar el fichero en cada uno.
     */
    QString profilePath() const;
    void scheduleProfileSave();
    void saveProfile();

    QString m_device;
    bool m_available = false;
    int m_brightness = 0;
    QTimer *m_saveTimer = nullptr;
    int m_maxBrightness = 100;
    QColor m_color = QColor(Qt::white);
    QColor m_secondaryColor = QColor(Qt::white);
    int m_effect = 0;
    int m_speed = 1;
};

class RgbBacklightPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface")

public:
    void registerTypes(const char *uri) override;
};
