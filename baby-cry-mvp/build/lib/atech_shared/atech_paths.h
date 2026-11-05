/**
 * @file atech_paths.h
 * @brief Folder path helpers for the microSD card (no Arduino dependency).
 *
 * MicroSDCard::pathJoin() / pathUp() forward here; kept free of Arduino so
 * tests/test_image_helpers.py can compile and check them on a host.
 */

#pragma once
#include <stdio.h>
#include <string.h>

namespace atech {

/** "/logs" + "a.csv" -> "/logs/a.csv"; "/" + "a.csv" -> "/a.csv". */
inline void pathJoin(const char* base, const char* name, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return;
    if (base == nullptr || base[0] == '\0') base = "/";
    if (name == nullptr) name = "";
    while (*name == '/') name++;
    size_t baseLen = strlen(base);
    bool slash = baseLen > 0 && base[baseLen - 1] == '/';
    snprintf(out, outSize, slash ? "%s%s" : "%s/%s", base, name);
}

/** Up one folder, in place: "/logs/2024" -> "/logs", "/logs" -> "/", "/" -> "/". */
inline void pathUp(char* path) {
    if (path == nullptr) return;
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';
    char* last = strrchr(path, '/');
    if (last == nullptr || last == path) {
        strcpy(path, "/");
    } else {
        *last = '\0';
    }
}

}  // namespace atech
