include(FetchContent)

set(LUA_INCLUDE_DIR "${IXRAY_SDK_INC}/lua/" CACHE PATH "" FORCE)
set(LUA_LIBRARIES "${LUAJIT_LIB}" CACHE FILEPATH "" FORCE)
set(LUA_LIBRARY "${LUAJIT_LIB}" CACHE FILEPATH "" FORCE)


add_library(Lua::Lua UNKNOWN IMPORTED)
set_target_properties(Lua::Lua PROPERTIES
    IMPORTED_LOCATION "${LUAJIT_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${IXRAY_SDK_INC}/lua/"
)

FetchContent_Declare(
    luabind
    GIT_REPOSITORY https://github.com/ForserX/luabind-latest.git
    GIT_TAG        128ba59572e508f0c550f4ff8a5235c07c77de3d
)

set(LUABIND_TESTAPP OFF CACHE BOOL "" FORCE)
set(LUABIND_USE_EXTERNAL_LUA OFF CACHE BOOL "" FORCE)

# LuaBind Debug
if (IXRAY_LDEBUG)
    set(LUABIND_DEBUG_SCRIPTS ON CACHE BOOL "" FORCE)
else()
    set(LUABIND_DEBUG_SCRIPTS OFF CACHE BOOL "" FORCE)
endif()

set(LUA_INCLUDE_DIR "${IXRAY_SDK_INC}/lua/")
set(LUA_LIB ${LUAJIT_LIB})

FetchContent_MakeAvailable(luabind)

option(ANTHOLOGY_LUA_API_CHECKS "Validate Lua binding arguments in the experimental port" OFF)
if(ANTHOLOGY_LUA_API_CHECKS)
    # PUBLIC keeps the luabind data layout identical in every consuming DLL.
    target_compile_definitions(luabind PUBLIC LUA_DEBUG)
    # Keep normal Lua nil-on-missing class lookup, including addon feature probes.
    # Only the lookup diagnostic is omitted; call/argument validation stays enabled.
    set(_class_rep "${luabind_SOURCE_DIR}/luabind/src/class_rep.cpp")
    file(READ "${_class_rep}" _class_rep_text)
    set(_missing_static "#ifndef LUABIND_NO_ERROR_CHECKING

\t{
\t\tstring_class msg = \"no static '")
    string(FIND "${_class_rep_text}" "${_missing_static}" _lookup_at)
    if(_lookup_at LESS 0)
        message(FATAL_ERROR "Pinned luabind missing-static lookup changed; review compatibility patch")
    endif()
    string(REPLACE "${_missing_static}" "#if 0 // Preserve nil lookup with checked calls

\t{
\t\tstring_class msg = \"no static '" _class_rep_text "${_class_rep_text}")
    set(_checked_rep "${CMAKE_BINARY_DIR}/anthology_class_rep.cpp")
    file(CONFIGURE OUTPUT "${_checked_rep}" CONTENT "${_class_rep_text}" @ONLY)
    get_target_property(_luabind_sources luabind SOURCES)
    list(REMOVE_ITEM _luabind_sources "${_class_rep}")
    set_property(TARGET luabind PROPERTY SOURCES "${_luabind_sources}")
    target_sources(luabind PRIVATE "${_checked_rep}")

endif()


set_target_properties(luabind PROPERTIES FOLDER "3rd Party")
