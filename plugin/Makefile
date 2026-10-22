# Build del plugin QML org.rgb.backlight
#
#   make
#   sudo make install DESTDIR=/usr/lib64/qt6/qml/org/rgb/backlight
#
QT_INC ?= /usr/include/qt6
MOC    ?= /usr/lib64/qt6/libexec/moc

PKG_CFLAGS := $(shell pkg-config --cflags Qt6Core Qt6Gui Qt6Qml Qt6DBus)
PKG_LIBS   := $(shell pkg-config --libs Qt6Core Qt6Gui Qt6Qml Qt6DBus)

CXXFLAGS := -fPIC -O2 -Wall -Wextra -std=c++17 $(PKG_CFLAGS) -I$(QT_INC) -I.
LDFLAGS  := -shared $(PKG_LIBS)

TARGET := librgbbacklightplugin.so

all: $(TARGET)

moc_rgbdevicecontrol.cpp: rgbdevicecontrol.h
	$(MOC) $< -o $@

rgbdevicecontrol.o: rgbdevicecontrol.cpp moc_rgbdevicecontrol.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): rgbdevicecontrol.o
	$(CXX) -o $@ $^ $(LDFLAGS)

install: $(TARGET)
	install -d $(DESTDIR)
	install -m 644 $(TARGET) $(DESTDIR)/
	install -m 644 qmldir $(DESTDIR)/

clean:
	rm -f *.o moc_*.cpp $(TARGET)

.PHONY: all clean install
