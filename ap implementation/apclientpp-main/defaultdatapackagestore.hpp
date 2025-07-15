/* Copyright (c) 2022 black-sliver, FelicitusNeko

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _DEFAULTDATAPACKAGESTORE_HPP
#define _DEFAULTDATAPACKAGESTORE_HPP

#include "apclient.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <time.h>

#if defined WIN32 || defined _WIN32
#include <shlobj.h>
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#ifdef __MAC_OS_X_VERSION_MIN_REQUIRED
#if __MAC_OS_X_VERSION_MIN_REQUIRED < 101500
#ifndef NO_STD_FILESYSTEM
#define NO_STD_FILESYSTEM
#endif
#endif
#endif

#if (defined(HAS_STD_FILESYSTEM) || (__cplusplus >= 201703L)) && !defined(NO_STD_FILESYSTEM)
#include <filesystem>
#else
#ifndef NO_STD_FILESYSTEM
#define NO_STD_FILESYSTEM
#endif
#include <string>
#include <errno.h>
#endif

class DefaultDataPackageStore : public APDataPackageStore
{
public:
#if defined WIN32 || defined _WIN32
    typedef std::wstring TString;
    typedef wchar_t TCHAR;
    static const wchar_t CSLASH = L'/';
#else
    typedef std::string TString;
    typedef char TCHAR;
    static const char CSLASH = '/';
#endif
private:

    typedef nlohmann::json json;
#ifndef NO_STD_FILESYSTEM
    typedef std::filesystem::path path;
#else
    class path final
    {
        TString s;

    public:
        path() = default;

        path(const TString& s)
            : s(s)
        {
        }

        path(const TCHAR* s)
            : s(s)
        {
        }

#if defined WIN32 || defined _WIN32
        // assume UTF8
        path(const std::string& s)
        {
            // NOTE: we assume utf8
            if (s.empty())
                return;

            auto sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
            if (sz < 1)
                return;

            wchar_t* wstr = new wchar_t[sz];
            memset(wstr, 0, sz * sizeof(*wstr));
            if (MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wstr, sz) != sz)
                goto cleanup;

            this->s = wstr;
cleanup:
            delete[] wstr;
        }

        std::string string() const
        {
            std::string res;
            auto sz = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
            if (sz < 1)
                return res;
            char* tmp = new char[sz];
            memset(tmp, 0, sz * sizeof(*tmp));
            if (WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, tmp, sz, NULL, NULL) != sz)
                goto done;

            res = tmp;
done:
            delete[] tmp;
            return res;
        }
#else
        const std::string& string() const
        {
            return s;
        }
#endif

        const TCHAR* c_str() const
        {
            return s.c_str();
        }

        bool empty() const
        {
            return s.empty();
        }

        path parent_path() const
        {
            if (s == slash())
                return slash();
            auto p = s.rfind(slash());
            if (p == s.npos)
                return {};
            return s.substr(0, p);
        }

        path operator/(const TString& other) const
        {
            return s + slash() + other;
        }

#if defined WIN32 || defined _WIN32
        path operator/(const std::string& other) const
        {
            return *this / path(other).s;
        }
#endif

        bool operator==(const TString& other) const
        {
            return s == other;
        }

        bool operator!=(const path& other) const
        {
            return s != other.s;
        }

        static const TCHAR* slash()
        {
            static const TCHAR val[] = {CSLASH, 0};
            return val;
        }
    };

    bool create_directories(const path& d, std::error_code& ec) noexcept
    {
        auto parent = d.parent_path();
        if (!parent.empty() && parent != d && !create_directories(parent, ec) && ec.value())
            return false;
#if defined WIN32 || defined _WIN32
        _wmkdir(d.c_str());
#else
        mkdir(d.c_str(), S_IRWXU);
#endif
        if (errno != EEXIST)
            ec = {errno, std::generic_category()};
        return !!errno;
    }
#endif

    path _path;


    void log(const char* msg)
    {
        printf("APClient: %s\n", msg);
    }

    path get_path(const std::string& game, const std::string& checksum) const
    {
        std::string safe_game, safe_checksum;
        const std::string exclude = "<>:\"/\\|?*";
        auto sanitize = [&](char c) { return exclude.find(c) == std::string::npos; };
        std::copy_if(game.begin(), game.end(), std::back_inserter(safe_game), sanitize);
        std::copy_if(checksum.begin(), checksum.end(), std::back_inserter(safe_checksum), sanitize);

        if (safe_game.empty() || safe_checksum != checksum)
            return {}; // invalid
        if (checksum.empty())
            return _path / (safe_game + ".json");
        return (_path / safe_game) / (safe_checksum + ".json");
    }

    static void touch(const path& filename)
    {
#if defined WIN32 || defined _WIN32
        struct __utimbuf64 ut;
        ut.actime = _time64(NULL);
        ut.modtime = ut.actime;
        _wutime64(filename.c_str(), &ut);
#else
        struct utimbuf ut;
        ut.actime = time(NULL);
        ut.modtime = ut.actime;
        utime(filename.c_str(), &ut);
#endif
    }

    static path get_default_cache_dir(const std::string& fallbackPath, const std::string& app = "Archipelago")
    {
#if defined WIN32 || defined _WIN32
        WCHAR appData[MAX_PATH];
        HRESULT hres = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData);
        if (hres == S_OK && *appData)
            return path(appData) / app / "Cache";
#elif defined __APPLE__
        const char* home = std::getenv("HOME");
        if (home)
            return path(home) / "Library" / "Caches" / app;
#else
#   if defined __EMSCRIPTEN__
        if (fallbackPath.empty()) { // HOME might not be persistent on emscripten
#   endif
        const char* xdg = std::getenv("XDG_CACHE_HOME");
        if (xdg)
            return path(xdg) / app;
        const char* home = std::getenv("HOME");
        if (home)
            return path(home) / ".cache" / app;
#   if defined __EMSCRIPTEN__
        }
#   endif
#endif
        if (!fallbackPath.empty())
            return path(fallbackPath) / "cache";
        return path("cache");
    }

public:
    DefaultDataPackageStore(const std::string fallbackPath = "")
        : _path(get_default_cache_dir(fallbackPath) / "datapackage")
    {
    }

    virtual bool load(const std::string& game, const std::string& checksum, json& data) override
    {
        auto p = get_path(game, checksum);
        if (p.empty()) {
            log("Could not determine datapackage cache location");
            return false;
        }
        try {
#ifdef NO_STD_FILESYSTEM
            std::ifstream f(p.c_str(), std::ios::binary);
#else
            std::ifstream f(p, std::ios::binary);
#endif
            if (f.fail() || f.eof())
                return false;
            data = json::parse(f);
            touch(p); // update file time to keep it in cache
            return true;
        } catch (const std::exception& ex) {
            log(("Failed to load " + p.string() + ":").c_str());
            log(ex.what());
            return false;
        }
    }

    virtual bool save(const std::string& game, const json& data) override
    {
#ifndef NO_STD_FILESYSTEM
        using std::filesystem::create_directories;
#endif

        if (!data.is_object())
            return false;
        auto it = data.find("checksum");
        path p;
        if (it == data.end())
            p = get_path(game, "");
        else
            p = get_path(game, *it);
        if (p.empty())
            return false;

        std::error_code ec;
        create_directories(p.parent_path(), ec);
        if (ec) {
            log(("Could not create " + p.parent_path().string() + ": " + ec.message()).c_str());
            return false;
        }

        try {
#ifdef NO_STD_FILESYSTEM
            std::ofstream f(p.c_str(), std::ios::binary);
#else
            std::ofstream f(p, std::ios::binary);
#endif
            f << data.dump();
            return true;
        } catch (const std::exception& ex) {
            log(ex.what());
            return false;
        }
    }
};


#endif // _DEFAULTDATAPACKAGESTORE
