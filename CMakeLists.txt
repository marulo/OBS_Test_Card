cmake_minimum_required(VERSION 3.16)

project(obs-test-card VERSION 0.1.0)

# Opciones de compilación
option(ENABLE_QT "Use Qt functionality" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# Nombre del archivo de salida (dll)
set(OUTPUT_NAME "obs-test-card")

# Platform-specific configurations
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

if(MSVC)
    # Flags recomendados para MSVC
    add_compile_options(/W4 /wd4100 /wd4996 /wd4201 /wd4458)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    
    # Usar Runtime Estático para evitar error de DLL faltante (VCRUNTIME140.dll etc)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# Buscar libobs
if(DEFINED LIBOBS_LIB AND DEFINED LIBOBS_INCLUDE_DIR)
    message(STATUS "Using provided libobs paths")
    add_library(OBS::libobs SHARED IMPORTED)
    set_target_properties(OBS::libobs PROPERTIES
        IMPORTED_IMPLIB "${LIBOBS_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR}"
    )
    if(DEFINED LIBOBS_CONFIG_DIR)
        set_property(TARGET OBS::libobs APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_CONFIG_DIR}")
    endif()
else()
    find_package(libobs REQUIRED)
endif()

# Include directories
include_directories(${LIBOBS_INCLUDE_DIRS})
# Asegurar que se puedan incluir cabeceras locales (como plugin-support.h)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Configurar archivo de soporte (versión/nombre)
configure_file(src/plugin-support.c.in src/plugin-support.c)

# Definir la librería del plugin
add_library(${PROJECT_NAME} MODULE
    src/plugin-main.c
    src/test-source.c
    ${CMAKE_CURRENT_BINARY_DIR}/src/plugin-support.c
)

# Linkear con libobs
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)

# Dependencias opcionales de QT
if(ENABLE_QT)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    
    find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
    
    # Find obs-frontend-api
    if(DEFINED LIBOBS_FRONTEND_API_LIB)
        add_library(OBS::frontend-api SHARED IMPORTED)
        set_target_properties(OBS::frontend-api PROPERTIES
            IMPORTED_IMPLIB "${LIBOBS_FRONTEND_API_LIB}"
        )
        if(DEFINED LIBOBS_FRONTEND_INCLUDE_DIR)
            set_property(TARGET OBS::frontend-api APPEND PROPERTY 
                INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_FRONTEND_INCLUDE_DIR}")
        endif()
    else()
        find_package(obs-frontend-api REQUIRED)
    endif()
    
    # Add dock widget source files
    target_sources(${PROJECT_NAME} PRIVATE
        src/test-card-dock.cpp
        src/test-card-dock-register.cpp
    )
    
    # Enable MOC for this target
    set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC ON)
    
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        Qt6::Core 
        Qt6::Widgets
        OBS::frontend-api
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_QT)
endif()

# Propiedades del target
set_target_properties(${PROJECT_NAME} PROPERTIES 
    OUTPUT_NAME "${OUTPUT_NAME}"
    PREFIX ""
)

# Copiar recursos (data) al directorio de salida para facilitar pruebas
# Solo si el directorio data existe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    file(GLOB_RECURSE RESOURCE_FILES "data/*")
    if(RESOURCE_FILES)
        file(COPY ${RESOURCE_FILES} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/data")
    endif()
endif()

# Install targets (cross-platform)
if(WIN32)
    install(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION "obs-plugins/64bit")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
        install(DIRECTORY data/ 
            DESTINATION "data/obs-plugins/${OUTPUT_NAME}")
    endif()
elseif(APPLE)
    install(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION "obs-plugins")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
        install(DIRECTORY data/ 
            DESTINATION "data/obs-plugins/${OUTPUT_NAME}")
    endif()
endif()

