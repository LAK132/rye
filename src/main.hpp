#ifndef RYE_MAIN_HPP
#define RYE_MAIN_HPP

#define LAK_BASIC_PROGRAM_IMGUI_WINDOW_IMPL
#include <lak/basic_program.hpp>

#include <lak/system/architecture.hpp>

#include "rye_git.hpp"
#define APP_VERSION GIT_TAG "-" GIT_HASH
#define APP_NAME    "RYE " STRINGIFY(LAK_ARCH) " " APP_VERSION

void credits();

#endif
