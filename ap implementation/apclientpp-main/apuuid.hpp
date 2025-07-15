/* Copyright (c) 2022 black-sliver

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _APUUID_HPP
#define _APUUID_HPP

/*
 * NOTE: this does not produce "real" UUIDs. AP just requires strings that are unlikely to collide.
 * This provides implementation for both browser context via emscripten as well as libc.
 * We use a singleton to properly initialize RNG once.
 * If calling srand() is a problem for the callee, you'll have to provide your own UUID generator.
 */

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <memory>
#include <fstream>


#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#include <stdlib.h>
#include <time.h>
#endif


namespace AP {
    class UUID {
    public:
        UUID(uint8_t bytes[16])
        {
            const char hex[] = "0123456789abcdef";
            _string.resize(32);
            char* p = (char*)_string.data();
            for (size_t i=0; i<16; i++) {
                *p = hex[(bytes[i] >> 4) & 0x0f];
                p++;
                *p = hex[bytes[i] & 0x0f];
                p++;
            }
        }

        virtual ~UUID() {}

        const std::string& string() const
        {
            return _string;
        }

    private:
        std::string _string;
    };

    class UUIDFactory {
    public:
        static UUIDFactory* instance();
        virtual ~UUIDFactory() {}
        void setFilename(const std::string& filename);
        UUID getPersistentUUID(const std::string& hostname_or_url);

    private:
        UUIDFactory();
        uint8_t randByte() const;
        UUID generate() const;
        void generate(uint8_t bytes[8]) const;
        uint8_t hash(const std::string& name) const;

        std::string _filename;
        std::fstream _f;
    };

    inline UUIDFactory* UUIDFactory::instance()
    {
        static auto singleton = std::unique_ptr<UUIDFactory>(new UUIDFactory());
        return singleton.get();
    }

    void UUIDFactory::setFilename(const std::string& filename)
    {
#if defined __EMSCRIPTEN__ && !defined USE_IDBFS
#error "IDBFS is currently required for emscripten"
#endif
        if (_filename == filename)
            return;
        if (_f.is_open())
            _f.close();
        _filename = filename;
    }

    UUID UUIDFactory::getPersistentUUID(const std::string& name)
    {
        if (!_f.is_open()) {
            if (_filename.empty())
                return generate();

            _f.open(_filename, std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::ate);
            if (!_f.is_open())
                _f.open(_filename, std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc);
            if (!_f.is_open()) {
                fprintf(stderr, "Could not write persistent UUID!\n");
                return generate();
            }
            if (_f.tellp() < 32) {
                // insert back compat string
                _f.seekp(0);
                _f << generate().string();
                _f.flush();
            }
            if (_f.tellp() < 64) {
                // insert old UUID into slot 0
                uint64_t state = 1;
                uint64_t dummy = 0;
                uint8_t bytes[16];
                _f.seekg(0);
                for (uint8_t i=0; i<16; i++) {
                    char c;
                    uint8_t b = 0;
                    for (uint8_t n=0; n<2; n++) {
                        b <<= 4;
                        _f.read(&c, 1);
                        if (c >= '0' && c <= '9')
                            b |= (uint8_t)(c - '0');
                        if (c >= 'a' && c <= 'f')
                            b |= (uint8_t)(c - 'a' + 0x0a);
                        if (c >= 'A' && c <= 'F')
                            b |= (uint8_t)(c - 'A' + 0x0a);
                    }
                    bytes[i] = b;
                }
                _f.seekp(32);
                _f.write((const char*)&state, sizeof(state));
                _f.write((const char*)&dummy, sizeof(dummy));
                _f.write((const char*)bytes, sizeof(bytes));
                _f.flush();
            }
        }
        // 32 byte at start of file reserved for back compat
        // then each entry is 8 byte state + 8 byte reserved + 16 byte uuid
        size_t offset = 32 + (size_t) hash(name) * 32;
        _f.seekg(offset);
        uint64_t state;
        uint64_t dummy;
        uint8_t bytes[16];

        // load existing UUID if it exists
        if (_f.read((char*)&state, sizeof(state)) && state
                && _f.read((char*)&dummy, sizeof(dummy))
                && _f.read((char*)bytes, sizeof(bytes)))
            return UUID(bytes);

        // expand file
        _f.clear();
        _f.seekp(0, std::fstream::end);
        dummy = 0;
        while (_f.tellp() < offset)
            if (!_f.write((const char*)&dummy, sizeof(dummy)))
                return generate(); // should never happen

        // write new UUID
        _f.seekp(offset);
        state = 1;
        generate(bytes);
        _f.write((const char*)&state, sizeof(state));
        _f.write((const char*)&dummy, sizeof(dummy));
        _f.write((const char*)&bytes, sizeof(bytes));
        _f.flush();
        return UUID(bytes);
    }

    void UUIDFactory::generate(uint8_t bytes[8]) const
    {
        for (size_t i=0; i<8; i++)
            bytes[i] = randByte();
    }

    UUID UUIDFactory::generate() const
    {
        uint8_t bytes[8];
        generate(bytes);
        return UUID(bytes);
    }

    uint8_t UUIDFactory::hash(const std::string& name) const
    {
        uint8_t res = 0;
        for (char c: name) {
            res = (res >> 7) | (res << 1);
            res = (uint8_t)(res + (uint8_t)c);
        }
        return res;
    }

#ifdef __EMSCRIPTEN__
    UUIDFactory::UUIDFactory()
    {
        /* js crypto needs no init from C */
    }

    uint8_t UUIDFactory::randByte() const
    {
        return (uint8_t)EM_ASM_INT({
            var buf = new Uint8Array(1);
            crypto.getRandomValues(buf);
            return buf[0];
        });
    }
#else
    UUIDFactory::UUIDFactory()
    {
        srand ((unsigned int) time (NULL));
    }

    uint8_t UUIDFactory::randByte() const
    {
        return rand();
    }
#endif
}

static std::string ap_get_uuid(const std::string& uuidFile, const std::string& host="")
{
    AP::UUIDFactory::instance()->setFilename(uuidFile);
    return AP::UUIDFactory::instance()->getPersistentUUID(host).string();
}

#endif // _APUUID_HPP
