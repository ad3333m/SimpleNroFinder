// Just enough zip reading to unpack homebrew packages: the central
// directory, stored and deflated entries, no zip64 or encryption.

#include "util/zip.h"

#include <cstdio>
#include <cstring>
#include <zlib.h>

namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;

u32 rd32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
u16 rd16(const u8* p) { return p[0] | (p[1] << 8); }

bool readAt(FILE* f, long off, void* buf, size_t len) {
    return fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

}  // namespace

std::vector<ZipEntry> zipList(const std::string& path) {
    std::vector<ZipEntry> out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    // The end-of-central-directory record sits in the last 64 KB + 22 bytes.
    long tail = size < 65557 ? size : 65557;
    std::vector<u8> buf(tail);
    if (!readAt(f, size - tail, buf.data(), tail)) { fclose(f); return out; }
    long eocd = -1;
    for (long i = tail - 22; i >= 0; i--)
        if (rd32(&buf[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { fclose(f); return out; }
    u16 count = rd16(&buf[eocd + 10]);
    u32 cdSize = rd32(&buf[eocd + 12]);
    u32 cdOff = rd32(&buf[eocd + 16]);
    std::vector<u8> cd(cdSize);
    if (!readAt(f, cdOff, cd.data(), cdSize)) { fclose(f); return out; }
    size_t p = 0;
    for (u16 i = 0; i < count && p + 46 <= cd.size(); i++) {
        if (rd32(&cd[p]) != 0x02014b50) break;
        ZipEntry e;
        e.method = rd16(&cd[p + 10]);
        e.compSize = rd32(&cd[p + 20]);
        e.size = rd32(&cd[p + 24]);
        u16 nameLen = rd16(&cd[p + 28]), extraLen = rd16(&cd[p + 30]), commentLen = rd16(&cd[p + 32]);
        e.localOffset = rd32(&cd[p + 42]);
        e.name.assign((const char*)&cd[p + 46], nameLen);
        out.push_back(e);
        p += 46 + nameLen + extraLen + commentLen;
    }
    fclose(f);
    return out;
}

bool zipExtract(const std::string& path, const ZipEntry& e, const std::string& dest) {
    if (e.method != 0 && e.method != 8) return false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    u8 local[30];
    if (!readAt(f, e.localOffset, local, 30) || rd32(local) != 0x04034b50) { fclose(f); return false; }
    long data = e.localOffset + 30 + rd16(&local[26]) + rd16(&local[28]);
    std::string tmp = dest + ".part";
    FILE* o = fopen(tmp.c_str(), "wb");
    if (!o || fseek(f, data, SEEK_SET) != 0) { if (o) fclose(o); fclose(f); return false; }

    // Streamed in 256 KB chunks: some packages carry files of several hundred MB.
    std::vector<u8> in(256 << 10), out(256 << 10);
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    bool ok = e.method == 0 || inflateInit2(&zs, -MAX_WBITS) == Z_OK;
    u32 left = e.compSize, written = 0;
    int rc = Z_OK;
    while (ok && left > 0) {
        size_t n = fread(in.data(), 1, left < in.size() ? left : in.size(), f);
        if (n == 0) { ok = false; break; }
        left -= n;
        if (e.method == 0) {
            ok = fwrite(in.data(), 1, n, o) == n;
            written += n;
            continue;
        }
        zs.next_in = in.data();
        zs.avail_in = n;
        do {
            zs.next_out = out.data();
            zs.avail_out = out.size();
            rc = inflate(&zs, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) { ok = false; break; }
            size_t got = out.size() - zs.avail_out;
            if (fwrite(out.data(), 1, got, o) != got) { ok = false; break; }
            written += got;
        } while (zs.avail_out == 0 && rc != Z_STREAM_END);
        if (rc == Z_STREAM_END) break;
    }
    if (e.method == 8) inflateEnd(&zs);
    fclose(f);
    ok = fclose(o) == 0 && ok && written == e.size;
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(dest.c_str());
    return rename(tmp.c_str(), dest.c_str()) == 0;
}
