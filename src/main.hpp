#ifndef RYE_MAIN_HPP
#define RYE_MAIN_HPP

#include <lak/architecture.hpp>

#include "rye_git.hpp"
#define APP_VERSION GIT_TAG "-" GIT_HASH
#define APP_NAME    "RYE " STRINGIFY(LAK_ARCH) " " APP_VERSION

#endif
