#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include "Log.hpp"
#include "GetRuntimeDir.hpp"

// taken from Hyprland/hyprctl/main.cpp
static  unsigned getUID() {
    const auto UID   = getuid();
    const auto PWUID = getpwuid(UID);
    return PWUID ? PWUID->pw_uid : UID;
}

static std::filesystem::path getRuntimeDir() {
    const auto XDG = getenv("XDG_RUNTIME_DIR");

    if (!XDG) {
        const auto USERID = std::to_string(getUID());
        return "/run/user/" + USERID + "/hypr";
    }

    return std::string{XDG} + "/hypr";
}

std::filesystem::path getHyprsunsetFolder() {
    std::filesystem::path lockFolder = getRuntimeDir();
    lockFolder += "/hyprsunset";

    if (!std::filesystem::exists(lockFolder)) {
        std::error_code e;
        std::filesystem::create_directory(lockFolder, e);
        if (e) {
            Debug::log(NONE, "✖ Failed to create {} folder: {}", lockFolder.string(), e.message());
            return {};
        }
    }

    return lockFolder;
}
