// SPDX-License-Identifier: GPL-2.0
/*
 * rgb-backlight
 *
 * Expone la retroiluminacion RGB de varios ratones y teclados USB como
 * dispositivos LED de la clase "leds", de modo que UPower (y por tanto
 * PowerDevil / Plasma) pueda controlarlos, y añade atributos extra
 * (color, efecto, velocidad) para el resto de ajustes.
 *
 * Dispositivos soportados:
 *
 *  - MSI "Mystic Light" (USB 1462:1562 y familia).  El teclado no
 *    implementa el LED estandar de Linux, asi que no existe ningun
 *    "kbd_backlight" que UPower pueda detectar.  Ademas el firmware solo
 *    aplica el brillo si el paquete incluye un color y nunca devuelve su
 *    estado, por lo que este driver toma el control del dispositivo HID
 *    (igual que hacen hid-asus/hid-razer en el arbol del kernel) e
 *    intercepta los paquetes que envian los programas (OpenRGB, MSI
 *    Center, ...) para poder reenviar el ultimo con el brillo nuevo sin
 *    perder el color ni el efecto.
 *
 *  - Razer Viper Mini (USB 1532:008a).  Aqui el brillo es un comando
 *    independiente, asi que no hace falta interceptar nada: el logo se
 *    controla directamente con el protocolo "Razer device command".
 *
 * Protocolo "Mystic Light" (teclado MSI), feature report 0x02 de 64 bytes:
 *
 *   [0] report id (0x02)
 *   [1] packet id
 *   [2] mode      (0 = off, 1 = static, 2 = breathing, 3 = flash,
 *                  4 = double flash, 5 = cycle, 6 = smooth cycle)
 *   [3] speed     (0 = low, 1 = medium, 2 = high)
 *   [4] brightness (0 = off, 1..10 = 10%..100%)
 *   [5] number of colors (0..7)
 *   [6..] colors (R, G, B each)
 *
 * Protocolo Razer (raton), feature report de 91 bytes:
 *
 *   [0] report id        [1] status         [2] transaction id (0x3F)
 *   [3..4] packets       [5] protocol       [6] data size
 *   [7] command class    [8] command id     [9..] argumentos
 *   [89] checksum (XOR de [3..88])          [90] reservado
 */

#include <linux/hid.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

/* ------------------------------------------------------------------ */
/* MSI Mystic Light                                                    */
/* ------------------------------------------------------------------ */

#define MSI_VENDOR_ID		0x1462
#define MSI_PRODUCT_ID		0x1562

#define MSI_REPORT_ID		0x02
#define MSI_REPORT_LEN		64

#define MSI_OFFSET_MODE		2
#define MSI_OFFSET_SPEED	3
#define MSI_OFFSET_BRIGHTNESS	4
#define MSI_OFFSET_NUM_COLORS	5
#define MSI_OFFSET_COLOR	6

#define MSI_MODE_OFF		0x00
#define MSI_MODE_STATIC		0x01
#define MSI_MODE_BREATHING	0x02
#define MSI_MODE_FLASHING	0x03
#define MSI_MODE_DOUBLE_FLASHING 0x04
#define MSI_MODE_CYCLE		0x05
#define MSI_MODE_SMOOTH_CYCLE	0x06
#define MSI_MODE_MAX		MSI_MODE_SMOOTH_CYCLE

#define MSI_SPEED_LOW		0
#define MSI_SPEED_MEDIUM	1
#define MSI_SPEED_HIGH		2
#define MSI_SPEED_MAX		MSI_SPEED_HIGH

#define MSI_BRIGHTNESS_MAX	10
#define MSI_MAX_COLORS		7

struct msi_kbd {
	struct hid_device *hdev;
	const struct hid_ll_driver *orig_ll;
	struct led_classdev led;
	unsigned char last_packet[MSI_REPORT_LEN];
	bool last_valid;
};

static bool msi_shutting_down;

static int msi_raw_request(struct hid_device *hdev, unsigned char reportnum,
			    u8 *buf, size_t len, unsigned char rtype,
			    int reqtype)
{
	struct msi_kbd *mk = hid_get_drvdata(hdev);

	if (mk && reqtype == HID_REQ_SET_REPORT &&
	    rtype == HID_FEATURE_REPORT && reportnum == MSI_REPORT_ID &&
	    len >= MSI_REPORT_LEN) {
		memcpy(mk->last_packet, buf, MSI_REPORT_LEN);
		mk->last_valid = true;
	}

	return mk->orig_ll->raw_request(hdev, reportnum, buf, len, rtype,
					reqtype);
}

struct msi_config {
	bool set_brightness;
	u8 brightness;
	bool set_mode;
	u8 mode;
	bool set_speed;
	u8 speed;
	bool set_colors;
	u8 num_colors;
	u8 colors[3 * MSI_MAX_COLORS];
};

static int msi_send(struct msi_kbd *mk, const struct msi_config *cfg)
{
	unsigned char *buf;
	int i, ret;

	buf = kmalloc(MSI_REPORT_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (mk->last_valid)
		memcpy(buf, mk->last_packet, MSI_REPORT_LEN);
	else {
		/* Estado por defecto del firmware: estatico, blanco y al maximo.
		 * Asi la primera escritura (p. ej. el color del perfil) no lo
		 * deja apagado hasta que llegue el brillo. */
		memset(buf, 0, MSI_REPORT_LEN);
		buf[0] = MSI_REPORT_ID;
		buf[MSI_OFFSET_MODE] = MSI_MODE_STATIC;
		buf[MSI_OFFSET_BRIGHTNESS] = MSI_BRIGHTNESS_MAX;
		buf[MSI_OFFSET_NUM_COLORS] = 1;
		buf[MSI_OFFSET_COLOR + 0] = 0xff;
		buf[MSI_OFFSET_COLOR + 1] = 0xff;
		buf[MSI_OFFSET_COLOR + 2] = 0xff;
	}

	buf[0] = MSI_REPORT_ID;

	if (cfg->set_brightness)
		buf[MSI_OFFSET_BRIGHTNESS] = cfg->brightness;
	if (cfg->set_mode)
		buf[MSI_OFFSET_MODE] = cfg->mode;
	if (cfg->set_speed)
		buf[MSI_OFFSET_SPEED] = cfg->speed;
	if (cfg->set_colors) {
		buf[MSI_OFFSET_NUM_COLORS] = cfg->num_colors;
		for (i = 0; i < cfg->num_colors && i < MSI_MAX_COLORS; i++) {
			buf[MSI_OFFSET_COLOR + i * 3 + 0] = cfg->colors[i * 3 + 0];
			buf[MSI_OFFSET_COLOR + i * 3 + 1] = cfg->colors[i * 3 + 1];
			buf[MSI_OFFSET_COLOR + i * 3 + 2] = cfg->colors[i * 3 + 2];
		}
	}

	ret = hid_hw_raw_request(mk->hdev, MSI_REPORT_ID, buf, MSI_REPORT_LEN,
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	kfree(buf);

	if (ret < 0)
		dev_err(&mk->hdev->dev,
			"failed to send the keyboard configuration: %d\n", ret);

	return ret < 0 ? ret : 0;
}

static int msi_led_set_brightness(struct led_classdev *ledc,
				   enum led_brightness brightness)
{
	struct msi_kbd *mk = container_of(ledc, struct msi_kbd, led);
	struct msi_config cfg = { .set_brightness = true,
				  .brightness = (u8)brightness };

	if (msi_shutting_down)
		return 0;

	return msi_send(mk, &cfg);
}

static struct msi_kbd *msi_attr_kbd(struct device *dev)
{
	struct led_classdev *ledc = dev_get_drvdata(dev);

	return container_of(ledc, struct msi_kbd, led);
}

static ssize_t colors_show(struct device *dev, struct device_attribute *attr,
			   char *buf);
static ssize_t colors_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count);
static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf);
static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count);
static ssize_t speed_show(struct device *dev, struct device_attribute *attr,
			  char *buf);
static ssize_t speed_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count);

/*
 * Los atributos se crean a mano (en vez de con los macros DEVICE_ATTR) para
 * poder usar el modo 0666: los macros prohíben los permisos "other
 * writable", pero aquí es inofensivo y evita tener que jugglingar grupos.
 */
static struct device_attribute dev_attr_colors = {
	.attr = { .name = "colors", .mode = 0664 },
	.show = colors_show,
	.store = colors_store,
};

static struct device_attribute dev_attr_mode = {
	.attr = { .name = "mode", .mode = 0664 },
	.show = mode_show,
	.store = mode_store,
};

static struct device_attribute dev_attr_speed = {
	.attr = { .name = "speed", .mode = 0664 },
	.show = speed_show,
	.store = speed_store,
};

static struct attribute *msi_led_attrs[] = {
	&dev_attr_colors.attr,
	&dev_attr_mode.attr,
	&dev_attr_speed.attr,
	NULL
};

static const struct attribute_group msi_led_attr_group = {
	.attrs = msi_led_attrs,
};

static const struct attribute_group *msi_led_groups[] = {
	&msi_led_attr_group,
	NULL
};

static ssize_t colors_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct msi_kbd *mk = msi_attr_kbd(dev);
	int i, off = 0, count;

	if (!mk->last_valid)
		return sysfs_emit(buf, "ffffff\n");

	count = mk->last_packet[MSI_OFFSET_NUM_COLORS];
	if (count > MSI_MAX_COLORS)
		count = MSI_MAX_COLORS;

	for (i = 0; i < count; i++) {
		const unsigned char *c = &mk->last_packet[MSI_OFFSET_COLOR + i * 3];

		off += scnprintf(buf + off, PAGE_SIZE - off, "%s%02x%02x%02x",
				 i ? "," : "", c[0], c[1], c[2]);
	}

	return off;
}

static ssize_t colors_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct msi_kbd *mk = msi_attr_kbd(dev);
	struct msi_config cfg = { .set_colors = true };
	const char *p = buf;
	int num = 0, ret;

	while (*p && num < MSI_MAX_COLORS) {
		unsigned int rgb = 0;
		char token[8];
		int len = 0;

		while (p[len] && p[len] != ',' && p[len] != '\n' && p[len] != ' ')
			len++;

		if (len <= 0 || len >= (int)sizeof(token))
			return -EINVAL;

		memcpy(token, p, len);
		token[len] = '\0';

		if (kstrtouint(token, 16, &rgb))
			return -EINVAL;

		cfg.colors[num * 3 + 0] = (rgb >> 16) & 0xff;
		cfg.colors[num * 3 + 1] = (rgb >> 8) & 0xff;
		cfg.colors[num * 3 + 2] = rgb & 0xff;
		cfg.num_colors = ++num;

		p += len;
		while (*p == ',' || *p == ' ' || *p == '\n')
			p++;
	}

	if (num == 0)
		return -EINVAL;

	ret = msi_send(mk, &cfg);
	return ret < 0 ? ret : count;
}

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct msi_kbd *mk = msi_attr_kbd(dev);

	if (!mk->last_valid)
		return sysfs_emit(buf, "%d\n", MSI_MODE_STATIC);

	return sysfs_emit(buf, "%d\n", mk->last_packet[MSI_OFFSET_MODE]);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct msi_kbd *mk = msi_attr_kbd(dev);
	struct msi_config cfg = { .set_mode = true };
	unsigned int val;
	int ret;

	if (kstrtouint(buf, 10, &val) || val > MSI_MODE_MAX)
		return -EINVAL;

	cfg.mode = (u8)val;
	ret = msi_send(mk, &cfg);
	return ret < 0 ? ret : count;
}

static ssize_t speed_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct msi_kbd *mk = msi_attr_kbd(dev);

	if (!mk->last_valid)
		return sysfs_emit(buf, "%d\n", MSI_SPEED_MEDIUM);

	return sysfs_emit(buf, "%d\n", mk->last_packet[MSI_OFFSET_SPEED]);
}

static ssize_t speed_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct msi_kbd *mk = msi_attr_kbd(dev);
	struct msi_config cfg = { .set_speed = true };
	unsigned int val;
	int ret;

	if (kstrtouint(buf, 10, &val) || val > MSI_SPEED_MAX)
		return -EINVAL;

	cfg.speed = (u8)val;
	ret = msi_send(mk, &cfg);
	return ret < 0 ? ret : count;
}

static int msi_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct msi_kbd *mk;
	struct hid_ll_driver *ll;
	int ret;

	mk = devm_kzalloc(&hdev->dev, sizeof(*mk), GFP_KERNEL);
	if (!mk)
		return -ENOMEM;

	hid_set_drvdata(hdev, mk);
	mk->hdev = hdev;

	ll = devm_kzalloc(&hdev->dev, sizeof(*ll), GFP_KERNEL);
	if (!ll)
		return -ENOMEM;

	mk->orig_ll = hdev->ll_driver;
	*ll = *hdev->ll_driver;
	ll->raw_request = msi_raw_request;

	ret = hid_parse(hdev);
	if (ret) {
		dev_err(&hdev->dev, "hid_parse failed: %d\n", ret);
		return ret;
	}

	hdev->ll_driver = ll;

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		dev_err(&hdev->dev, "hid_hw_start failed: %d\n", ret);
		hdev->ll_driver = mk->orig_ll;
		return ret;
	}

	mk->led.name = "msi::kbd_backlight";
	mk->led.max_brightness = MSI_BRIGHTNESS_MAX;
	mk->led.brightness = MSI_BRIGHTNESS_MAX;
	mk->led.brightness_set_blocking = msi_led_set_brightness;
	mk->led.groups = msi_led_groups;

	ret = led_classdev_register(&hdev->dev, &mk->led);
	if (ret) {
		dev_err(&hdev->dev, "led_classdev_register failed: %d\n", ret);
		hid_hw_stop(hdev);
		hdev->ll_driver = mk->orig_ll;
		return ret;
	}

	/* No se manda nada al detectar el teclado: se enciende con lo que
	 * tenga el firmware y el perfil (rgb-backlight-restore.service) lo
	 * pone como estaba.  El primer paquete se construye a partir del
	 * estado por defecto del firmware. */

	dev_info(&hdev->dev, "backlight exposed as LED '%s' (max %u)\n",
		 mk->led.name, mk->led.max_brightness);
	return 0;
}

static void msi_remove(struct hid_device *hdev)
{
	struct msi_kbd *mk = hid_get_drvdata(hdev);

	if (mk) {
		led_classdev_unregister(&mk->led);

		/* Restore the original low level driver: our copy lives in
		 * the module's memory, which goes away when it is unloaded. */
		hdev->ll_driver = mk->orig_ll;
	}

	/* Mandatory: stop the low level driver, otherwise the module
	 * cannot be removed (the USB URBs would still be running). */
	hid_hw_stop(hdev);
}

static const struct hid_device_id msi_id_table[] = {
	{ HID_USB_DEVICE(MSI_VENDOR_ID, MSI_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, msi_id_table);

static struct hid_driver msi_driver = {
	.name = "rgb-backlight-msi",
	.probe = msi_probe,
	.remove = msi_remove,
	.id_table = msi_id_table,
};

/* ------------------------------------------------------------------ */
/* Razer Viper Mini                                                    */
/* ------------------------------------------------------------------ */

#define RAZER_VENDOR_ID		0x1532
#define RAZER_PRODUCT_ID	0x008a

#define RAZER_REPORT_LEN	91
/* Protocolo Razer (formato "extended matrix", segun OpenMouse/OpenRazer) */
#define RAZER_LED_LOGO		0x04
#define RAZER_STORAGE		0x01
#define RAZER_TRANSACTION_ID	0x3f	/* la iluminacion responde con 0x3f */

#define RAZER_CLASS_MATRIX	0x0f
#define RAZER_CMD_MATRIX_EFFECT	0x02
#define RAZER_CMD_MATRIX_BRIGHTNESS	0x04

/* ids de efecto */
#define RAZER_EFFECT_OFF	0x00
#define RAZER_EFFECT_STATIC	0x01
#define RAZER_EFFECT_BREATHING	0x02
#define RAZER_EFFECT_SPECTRUM	0x03
#define RAZER_EFFECT_WAVE	0x04
#define RAZER_EFFECT_REACTIVE	0x05

/* indices en el desplegable de la interfaz */
#define RAZER_UI_OFF		0
#define RAZER_UI_STATIC		1
#define RAZER_UI_SPECTRUM	2
#define RAZER_UI_REACTIVE	3
#define RAZER_UI_BREATH_RANDOM	4
#define RAZER_UI_BREATH_SINGLE	5
#define RAZER_UI_BREATH_DUAL	6
#define RAZER_UI_MAX		RAZER_UI_BREATH_DUAL
#define RAZER_EFFECT_MAX	RAZER_UI_MAX

#define RAZER_WAVE_DIRECTION	0x01
#define RAZER_WAVE_SPEED	0x28

#define RAZER_BRIGHTNESS_MAX	100

struct razer_mouse {
	struct platform_device *pdev;
	struct led_classdev led;
	u32 color;
	u32 secondaryColor;
	u8 effect;
	u8 speed;
	u8 brightness;
};

static struct razer_mouse razer;

/* Construye un paquete del protocolo Razer (91 bytes + checksum). */
static void razer_build(u8 *buf, u8 cmd_class, u8 cmd_id, u8 size,
			const u8 *args, u8 nargs)
{
	unsigned int i, crc = 0;

	memset(buf, 0, RAZER_REPORT_LEN);
	buf[2] = RAZER_TRANSACTION_ID;
	buf[6] = size;
	buf[7] = cmd_class;
	buf[8] = cmd_id;
	if (nargs)
		memcpy(&buf[9], args, nargs);

	for (i = 3; i < 89; i++)
		crc ^= buf[i];
	buf[89] = crc;
}

/*
 * El firmware del raton ignora los comandos que llegan por una interfaz que
 * no los gestiona, asi que se envia el paquete por todas las interfaces HID
 * del dispositivo (el kernel no deja enviar un feature report que el
 * descriptor no declara, por eso esto solo se puede hacer desde el modulo).
 */
static int razer_send_one(struct device *dev, void *data)
{
	const u8 *pkt = data;
	struct hid_device *hdev = to_hid_device(dev);
	u8 *buf;
	int ret;

	if (hdev->vendor != RAZER_VENDOR_ID ||
	    hdev->product != RAZER_PRODUCT_ID)
		return 0;

	buf = kmalloc(RAZER_REPORT_LEN, GFP_KERNEL);
	if (!buf)
		return 0;

	memcpy(buf, pkt, RAZER_REPORT_LEN);
	/* Mejor esfuerzo: si otro dispositivo Razer no acepta el paquete, se
	 * ignora; el nuestro se comprueba al leer su estado. */
	ret = hid_hw_raw_request(hdev, buf[0], buf, RAZER_REPORT_LEN,
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	(void)ret;
	kfree(buf);

	return 0;
}

static int razer_broadcast(const u8 *pkt)
{
	return bus_for_each_dev(&hid_bus_type, NULL, (void *)pkt,
				 razer_send_one);
}

/* Hay algun raton Razer conectado? */
static int razer_find_cb(struct device *dev, void *data)
{
	struct hid_device *hdev = to_hid_device(dev);

	if (hdev->vendor == RAZER_VENDOR_ID &&
	    hdev->product == RAZER_PRODUCT_ID)
		return 1;

	return 0;
}

static bool razer_present(void)
{
	return bus_for_each_dev(&hid_bus_type, NULL, NULL, razer_find_cb) > 0;
}

static int razer_send(const u8 *buf)
{
	if (!razer_present())
		return -ENODEV;

	return razer_broadcast(buf);
}

/*
 * Aplica el aspecto al logo del raton.
 *
 * Formato "extended matrix" de Razer (clase 0x0f), tal y como documenta
 * OpenMouse (src/razer/codec.ts) y OpenRazer:
 *
 *   clase 0x0f / cmd 0x04 -> brillo  (args: storage, led, 0..255)
 *   clase 0x0f / cmd 0x02 -> efecto  (args: storage, led, id, ...)
 */
static int razer_apply(void)
{
	u8 buf[RAZER_REPORT_LEN];
	u8 args[12];
	u8 rgb[3], rgb2[3];
	int ret = 0;

	if (!razer_present())
		return -ENODEV;

	rgb[0] = (u8)(razer.color >> 16);
	rgb[1] = (u8)(razer.color >> 8);
	rgb[2] = (u8)razer.color;
	rgb2[0] = (u8)(razer.secondaryColor >> 16);
	rgb2[1] = (u8)(razer.secondaryColor >> 8);
	rgb2[2] = (u8)razer.secondaryColor;

	/* ---- brillo ---- */
	memset(args, 0, sizeof(args));
	args[0] = RAZER_STORAGE;
	args[1] = RAZER_LED_LOGO;
	args[2] = (u8)((unsigned int)razer.brightness * 255 / RAZER_BRIGHTNESS_MAX);
	razer_build(buf, RAZER_CLASS_MATRIX, RAZER_CMD_MATRIX_BRIGHTNESS,
		    0x03, args, 0x03);
	if (razer_send(buf) < 0)
		ret = -EIO;

	/* ---- efecto ---- */
	memset(args, 0, sizeof(args));
	args[0] = RAZER_STORAGE;
	args[1] = RAZER_LED_LOGO;

	switch (razer.effect) {
	case RAZER_UI_STATIC:
		args[2] = RAZER_EFFECT_STATIC;
		args[3] = 0x00;
		args[4] = 0x00;
		args[5] = 0x01;
		args[6] = rgb[0];
		args[7] = rgb[1];
		args[8] = rgb[2];
		razer_build(buf, RAZER_CLASS_MATRIX, RAZER_CMD_MATRIX_EFFECT,
			    0x09, args, 0x09);
		break;
	case RAZER_UI_SPECTRUM:
		args[2] = RAZER_EFFECT_SPECTRUM;
		args[3] = 0x00;
		args[4] = 0x00;
		args[5] = 0x00;
		razer_build(buf, RAZER_CLASS_MATRIX, RAZER_CMD_MATRIX_EFFECT,
			    0x06, args, 0x06);
		break;
	case RAZER_UI_REACTIVE:
		args[2] = RAZER_EFFECT_REACTIVE;
		args[3] = 0x00;
		args[4] = (u8)(razer.speed + 1);	/* 1..4 */
		args[5] = 0x01;
		args[6] = rgb[0];
		args[7] = rgb[1];
		args[8] = rgb[2];
		razer_build(buf, RAZER_CLASS_MATRIX, RAZER_CMD_MATRIX_EFFECT,
			    0x09, args, 0x09);
		break;
	case RAZER_UI_BREATH_RANDOM:
		args[2] = RAZER_EFFECT_BREATHING;
		args[3] = 0x00;
		args[4] = 0x00;
		args[5] = 0x00;
		razer_build(buf, RAZER_CLASS_MATRIX, RAZER_CMD_MATRIX_EFFECT,
			    0x06, args, 0x06);
		break;
	case RAZER_UI_BREATH_SINGLE:
		args[2] = RAZER_EFFECT_BREATHING;
		args[3] = 0x01;	/* un color */
		args[4] = 0x00;
		args[5] = 0x01;
		args[6] = rgb[0];
		args[7] = rgb[1];
		args[8] = rgb[2];
		razer_build(buf, RAZER_CLASS_MATRIX, RAZER_CMD_MATRIX_EFFECT,
			    0x09, args, 0x09);
		break;
	case RAZER_UI_BREATH_DUAL:
		args[2] = RAZER_EFFECT_BREATHING;
		args[3] = 0x02;	/* dos colores */
		args[4] = 0x00;
		args[5] = 0x02;
		args[6] = rgb[0];
		args[7] = rgb[1];
		args[8] = rgb[2];
		args[9] = rgb2[0];
		args[10] = rgb2[1];
		args[11] = rgb2[2];
		razer_build(buf, RAZER_CLASS_MATRIX, RAZER_CMD_MATRIX_EFFECT,
			    0x0c, args, 0x0c);
		break;
	case RAZER_UI_OFF:
	default:
		args[2] = RAZER_EFFECT_OFF;
		args[3] = 0x00;
		args[4] = 0x00;
		args[5] = 0x00;
		razer_build(buf, RAZER_CLASS_MATRIX, RAZER_CMD_MATRIX_EFFECT,
			    0x06, args, 0x06);
		break;
	}

	if (razer_send(buf) < 0)
		ret = -EIO;

	return ret;
}

static int razer_led_set_brightness(struct led_classdev *ledc,
				     enum led_brightness brightness)
{
	razer.brightness = (u8)brightness;
	return razer_apply();
}

static struct razer_mouse *razer_attr_mouse(struct device *dev)
{
	struct led_classdev *ledc = dev_get_drvdata(dev);

	return container_of(ledc, struct razer_mouse, led);
}

static ssize_t razer_color_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct razer_mouse *rm = razer_attr_mouse(dev);

	return sysfs_emit(buf, "%06x\n", rm->color);
}

static ssize_t razer_color_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct razer_mouse *rm = razer_attr_mouse(dev);
	char token[8];
	unsigned int rgb = 0;
	int len = 0, ret;

	while (buf[len] && buf[len] != '\n' && buf[len] != ' ')
		len++;

	if (len <= 0 || len >= (int)sizeof(token))
		return -EINVAL;

	memcpy(token, buf, len);
	token[len] = '\0';

	if (kstrtouint(token, 16, &rgb))
		return -EINVAL;

	rm->color = rgb & 0xffffff;
	ret = razer_apply();
	return ret < 0 ? ret : count;
}

static ssize_t razer_effect_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct razer_mouse *rm = razer_attr_mouse(dev);

	return sysfs_emit(buf, "%d\n", rm->effect);
}

static ssize_t razer_effect_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct razer_mouse *rm = razer_attr_mouse(dev);
	unsigned int val;
	int ret;

	if (kstrtouint(buf, 10, &val) || val > RAZER_EFFECT_MAX)
		return -EINVAL;

	rm->effect = (u8)val;
	ret = razer_apply();
	return ret < 0 ? ret : count;
}

static struct device_attribute dev_attr_razer_color = {
	.attr = { .name = "color", .mode = 0664 },
	.show = razer_color_show,
	.store = razer_color_store,
};

static struct device_attribute dev_attr_razer_effect = {
	.attr = { .name = "effect", .mode = 0664 },
	.show = razer_effect_show,
	.store = razer_effect_store,
};

static ssize_t razer_color2_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct razer_mouse *rm = razer_attr_mouse(dev);

	return sysfs_emit(buf, "%06x\n", rm->secondaryColor);
}

static ssize_t razer_color2_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct razer_mouse *rm = razer_attr_mouse(dev);
	char token[8];
	unsigned int rgb = 0;
	int len = 0, ret;

	while (buf[len] && buf[len] != '\n' && buf[len] != ' ')
		len++;

	if (len <= 0 || len >= (int)sizeof(token))
		return -EINVAL;

	memcpy(token, buf, len);
	token[len] = '\0';

	if (kstrtouint(token, 16, &rgb))
		return -EINVAL;

	rm->secondaryColor = rgb & 0xffffff;
	ret = razer_apply();
	return ret < 0 ? ret : count;
}

static ssize_t razer_speed_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct razer_mouse *rm = razer_attr_mouse(dev);

	return sysfs_emit(buf, "%d\n", rm->speed);
}

static ssize_t razer_speed_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct razer_mouse *rm = razer_attr_mouse(dev);
	unsigned int val;
	int ret;

	if (kstrtouint(buf, 10, &val) || val > 3)
		return -EINVAL;

	rm->speed = (u8)val;
	ret = razer_apply();
	return ret < 0 ? ret : count;
}

static struct device_attribute dev_attr_razer_color2 = {
	.attr = { .name = "color2", .mode = 0664 },
	.show = razer_color2_show,
	.store = razer_color2_store,
};

static struct device_attribute dev_attr_razer_speed = {
	.attr = { .name = "speed", .mode = 0664 },
	.show = razer_speed_show,
	.store = razer_speed_store,
};

static struct attribute *razer_led_attrs[] = {
	&dev_attr_razer_color.attr,
	&dev_attr_razer_color2.attr,
	&dev_attr_razer_effect.attr,
	&dev_attr_razer_speed.attr,
	NULL
};

static const struct attribute_group razer_led_attr_group = {
	.attrs = razer_led_attrs,
};

static const struct attribute_group *razer_led_groups[] = {
	&razer_led_attr_group,
	NULL
};

static int razer_probe(struct platform_device *pdev)
{
	int ret;

	razer.pdev = pdev;
	razer.color = 0xffffff;
	razer.secondaryColor = 0xffffff;
	razer.effect = RAZER_UI_STATIC;
	razer.speed = 1;
	razer.brightness = RAZER_BRIGHTNESS_MAX;

	razer.led.name = "razer::logo_backlight";
	razer.led.max_brightness = RAZER_BRIGHTNESS_MAX;
	razer.led.brightness = RAZER_BRIGHTNESS_MAX;
	razer.led.brightness_set_blocking = razer_led_set_brightness;
	razer.led.groups = razer_led_groups;

	ret = led_classdev_register(&pdev->dev, &razer.led);
	if (ret) {
		platform_device_unregister(pdev);
		razer.pdev = NULL;
		return ret;
	}

	dev_info(&pdev->dev, "Razer logo exposed as LED '%s'%s\n",
		 razer.led.name, razer_present() ? "" : " (mouse not connected)");
	return 0;
}

static void razer_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&razer.led);
}

static struct platform_driver razer_driver = {
	.probe = razer_probe,
	.remove = razer_remove,
	.driver = { .name = "rgb-backlight-razer" },
};

/* ------------------------------------------------------------------ */

static int __init rgb_backlight_init(void)
{
	int ret;

	ret = hid_register_driver(&msi_driver);
	if (ret)
		return ret;

	/* Dispositivo virtual para publicar el LED del raton Razer */
	razer.pdev = platform_device_alloc("rgb-backlight-razer",
					   PLATFORM_DEVID_NONE);
	if (!razer.pdev) {
		hid_unregister_driver(&msi_driver);
		return -ENOMEM;
	}

	ret = platform_device_add(razer.pdev);
	if (ret) {
		platform_device_put(razer.pdev);
		razer.pdev = NULL;
		hid_unregister_driver(&msi_driver);
		return ret;
	}

	ret = platform_driver_register(&razer_driver);
	if (ret) {
		platform_device_unregister(razer.pdev);
		razer.pdev = NULL;
		hid_unregister_driver(&msi_driver);
		return ret;
	}

	return 0;
}

static void __exit rgb_backlight_exit(void)
{
	/* Do not switch the devices off while unloading the module. */
	msi_shutting_down = true;
	platform_driver_unregister(&razer_driver);
	if (razer.pdev) {
		platform_device_unregister(razer.pdev);
		razer.pdev = NULL;
	}
	hid_unregister_driver(&msi_driver);
}

module_init(rgb_backlight_init);
module_exit(rgb_backlight_exit);

MODULE_AUTHOR("OpenCode");
MODULE_DESCRIPTION("RGB backlights of USB gaming keyboards and mice as LED class devices");
MODULE_LICENSE("GPL");
MODULE_VERSION("3.0");
