cmake_minimum_required(VERSION 2.8)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
set(CONFIG_FILE_PATH "$ENV{HOME}/.config/qlamz/")

# project name
project(qlamz)

# all sources
set(SOURCES
    src/main.cpp
    src/qlamz.cpp
    src/Settings.cpp
    src/About.cpp
    src/Error.cpp
    src/TrackModel.cpp
    src/Track.cpp
    src/CheckboxDelegate.cpp
    src/ProgressDelegate.cpp
    src/TrackDownloader.cpp
)

# moc header files
set(MOC_HEADERS
    src/qlamz.h
    src/Settings.h
    src/About.h
    src/Error.h
    src/TrackModel.h
    src/Track.h
    src/CheckboxDelegate.h
    src/ProgressDelegate.h
    src/TrackDownloader.h
)

# ui files
set(UIS
    ui/qlamz.ui
    ui/Settings.ui
    ui/About.ui
    ui/Error.ui
)

# ressource files
set(RESSOURCES
    ressource/qlamz.qrc
)

#translation files
set(TRANSLATIONS
    translation/qlamz_de.ts
    translation/qlamz_en.ts
)

# custom definitions for gcc
add_definitions(-Wall)

# apply some qt moduls as only QtCore and QtGui are enabled by default
set(QT_USE_QTXML TRUE)
set(QT_USE_QTNETWORK TRUE)
set(QT_USE_QTWEBKIT TRUE)

# find the qt package
find_package(Qt4 REQUIRED)
find_package(LibAmz REQUIRED)

# include all qt header files
include(${QT_USE_FILE})

# run some qt wrapping code
qt4_add_resources(RESSOURCE_SOURCES ${RESSOURCES})

qt4_wrap_ui(UI_HEADERS ${UIS})
qt4_wrap_cpp(MOC_SOURCES ${MOC_HEADERS})

qt4_create_translation(TRANSLATION ${SOURCES} ${MOC_HEADERS} ${UI_HEADERS} ${TRANSLATIONS})
qt4_add_translation(TRANSLATION_SOURCES ${TRANSLATIONS})

# some other includes
include_directories(${CMAKE_BINARY_DIR} ${LIBAMZ_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)

# set some config options
set(INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(SHARE_PATH ${CMAKE_INSTALL_PREFIX}/share)
configure_file(cmake/config.h.in config.h)

# create the executables
add_executable(qlamz ${SOURCES} ${MOC_SOURCES} ${RESSOURCE_SOURCES} ${UI_HEADERS}
    ${TRANSLATION_SOURCES})

# link them to one file
target_link_libraries(qlamz ${QT_LIBRARIES} ${LIBAMZ_LIBRARIES})

# install the program
install(TARGETS qlamz RUNTIME DESTINATION bin)

# install the amazon informations file
install(FILES config/amazon DESTINATION share/qlamz)

# install icons
install(FILES icons/29119-amazon.png DESTINATION share/pixmaps)

# install desktop file
install(FILES qlamz.desktop DESTINATION share/applications)
