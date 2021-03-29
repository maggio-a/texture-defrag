include(../base.pri)

SOURCES += \
    ../src/intersection.cpp \
    ../src/mesh_attribute.cpp \
    ../src/packing.cpp \
    ../src/seam_remover.cpp \
    ../src/seams.cpp \
    ../src/texture_optimization.cpp \
    ../src/mesh_graph.cpp \
    ../src/gl_utils.cpp \
    ../src/mesh.cpp \
    ../src/texture_rendering.cpp \
    ../src/logging.cpp \
    ../src/matching.cpp \
    ../src/arap.cpp \
    ../src/shell.cpp \
    ../src/texture_object.cpp \
    main.cpp

SOURCES += \
    $$VCGPATH/wrap/openfbx/src/ofbx.cpp \
    $$VCGPATH/wrap/openfbx/src/miniz.c \
    $$VCGPATH/wrap/ply/plylib.cpp \
    $$VCGPATH/wrap/qt/outline2_rasterizer.cpp \

HEADERS += \
    ../src/intersection.h \
    ../src/mesh.h \
    ../src/packing.h \
    ../src/seam_remover.h \
    ../src/seams.h \
    ../src/timer.h \
    ../src/types.h \
    ../src/mesh_graph.h \
    ../src/texture_rendering.h \
    ../src/math_utils.h \
    ../src/texture_optimization.h \
    ../src/pushpull.h \
    ../src/gl_utils.h \
    ../src/mesh_attribute.h \
    ../src/logging.h \
    ../src/utils.h \
    ../src/matching.h \
    ../src/arap.h \
    ../src/shell.h \
    ../src/texture_object.h
