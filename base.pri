VCGPATH = $$PWD/vcglib

CONFIG += console
CONFIG += c++11
CONFIG -= app_bundle

#### QT STUFF ##################################################################

TEMPLATE = app
QT = core gui svg

##### INCLUDE PATH #############################################################

INCLUDEPATH += $$PWD/src $$VCGPATH $$VCGPATH/eigenlib

#### PLATFORM SPECIFIC #########################################################

unix {
  LIBS += -lGLU
}

win32 {
  LIBS += -lopengl32 -lglu32
  DEFINES += NOMINMAX
}
