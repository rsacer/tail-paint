cmake_minimum_required(VERSION 3.16)

project(gnome-paint
    VERSION 1.0.0
    DESCRIPTION "Um aplicativo de pintura simples no estilo GNOME"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt gera código (moc/uic/rcc) automaticamente
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core Svg)

add_executable(gnome-paint
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/Canvas.cpp
    src/Canvas.h
    src/IconLoader.cpp
    src/IconLoader.h
    resources.qrc
)

target_link_libraries(gnome-paint PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::Core
    Qt6::Svg
)

install(TARGETS gnome-paint RUNTIME DESTINATION bin)

# Arquivos para a integração com o desktop (usados na geração do AppImage).
install(FILES packaging/gnome-paint.desktop
    DESTINATION share/applications)
install(FILES packaging/gnome-paint.png
    DESTINATION share/icons/hicolor/512x512/apps)
