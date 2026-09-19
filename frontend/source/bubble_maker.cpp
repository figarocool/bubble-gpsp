//
// Vita "game bubble" generator implementation for Emu4VitaPlus.
// See bubble_maker.h for context.
//

#include "bubble_maker.h"

#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>

#include "sha1.h"
#include "head_bin.h"
#include "file.h"
#include "global.h"
#include "network.h"
#include "rom_name.h"
#include "core_spec/defines.h"

#include <png.h>

#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>
#include <algorithm>

static void blog(const char *fmt, ...)
{
    FILE *f = fopen((std::string(CORE_DATA_DIR) + "/bubble_debug.log").c_str(), "a");
    if (!f)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

// tiny FNV-1a hash, used only to derive a stable 9-char titleid from the game name
static uint32_t fnv1a(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s)
    {
        h ^= (uint8_t)c;
        h *= 16777619u;
    }
    return h;
}

static std::string MakeTitleId(const std::string &name)
{
    uint32_t h = fnv1a(name);
    char buf[10];
    // "GB" prefix + 7 hex digits = 9 chars total, matching vita titleid length
    snprintf(buf, sizeof(buf), "GB%07X", h & 0x0FFFFFFF);
    return std::string(buf);
}

// PSF (param.sfo) format: 20 byte header, then N x 16 byte index entries,
// then the key table (null terminated strings), then the data table. We only
// ever patch existing string fields in place, keeping every offset and every
// other field byte-identical to the real, working param.sfo this app was
// installed with.
static bool PatchSfo(std::vector<char> &sfo, const std::string &title_id, const std::string &title)
{
    if (sfo.size() < 20)
        return false;

    // the base app's param.sfo ships an empty CONTENT_ID - left untouched,
    // every cloned bubble would share that same (empty) content id, and the
    // Vita shell caches LiveArea grid icons keyed by content id rather than
    // title id: the 2nd+ bubble created would then silently reuse whatever
    // icon got cached for the 1st one's (identical, empty) content id,
    // showing no grid icon of its own even though everything else (the
    // title, the LiveArea background) is correctly per-bubble.
    char content_id[48];
    snprintf(content_id, sizeof(content_id), "EP9000-%s_00-0000000000000000", title_id.c_str());
    const std::string content_id_str(content_id);

    auto readU32 = [&](size_t off)
    {
        uint32_t v;
        memcpy(&v, sfo.data() + off, 4);
        return v;
    };

    uint32_t key_table_start = readU32(8);
    uint32_t data_table_start = readU32(12);
    uint32_t count = readU32(16);

    for (uint32_t i = 0; i < count; i++)
    {
        size_t entry_off = 20 + i * 16;
        if (entry_off + 16 > sfo.size())
            break;

        uint16_t key_offset;
        uint32_t data_len, data_max_len, data_offset;
        memcpy(&key_offset, sfo.data() + entry_off, 2);
        memcpy(&data_len, sfo.data() + entry_off + 4, 4);
        memcpy(&data_max_len, sfo.data() + entry_off + 8, 4);
        memcpy(&data_offset, sfo.data() + entry_off + 12, 4);

        size_t key_start = key_table_start + key_offset;
        if (key_start >= sfo.size())
            continue;
        std::string key(sfo.data() + key_start);

        const std::string *new_value = nullptr;
        if (key == "TITLE_ID")
            new_value = &title_id;
        else if (key == "TITLE")
            new_value = &title;
        else if (key == "STITLE")
            new_value = &title;
        else if (key == "CONTENT_ID")
            new_value = &content_id_str;

        if (!new_value)
            continue;

        size_t data_start = data_table_start + data_offset;
        if (data_start + data_max_len > sfo.size())
            continue;

        std::string v = *new_value;
        if (v.size() + 1 > data_max_len)
            v.resize(data_max_len - 1);

        memset(sfo.data() + data_start, 0, data_max_len);
        memcpy(sfo.data() + data_start, v.c_str(), v.size() + 1);

        uint32_t new_len = (uint32_t)v.size() + 1;
        memcpy(sfo.data() + entry_off + 4, &new_len, 4);
    }

    return true;
}

static std::string SfoGetString(const std::vector<char> &sfo, const char *want_key)
{
    if (sfo.size() < 20)
        return "";
    uint32_t key_table_start, data_table_start, count;
    memcpy(&key_table_start, sfo.data() + 8, 4);
    memcpy(&data_table_start, sfo.data() + 12, 4);
    memcpy(&count, sfo.data() + 16, 4);

    for (uint32_t i = 0; i < count; i++)
    {
        size_t entry_off = 20 + i * 16;
        if (entry_off + 16 > sfo.size())
            break;
        uint16_t key_offset;
        uint32_t data_offset;
        memcpy(&key_offset, sfo.data() + entry_off, 2);
        memcpy(&data_offset, sfo.data() + entry_off + 12, 4);

        size_t key_start = key_table_start + key_offset;
        if (key_start >= sfo.size())
            continue;
        std::string key(sfo.data() + key_start);
        if (key != want_key)
            continue;

        size_t data_start = data_table_start + data_offset;
        if (data_start >= sfo.size())
            continue;
        return std::string(sfo.data() + data_start);
    }
    return "";
}

#define ntohl_local __builtin_bswap32

// port of VitaDeploy's promote.c fpkg_hmac() - a fixed, non-standard
// checksum scheme the promoter service expects inside sce_sys/package/head.bin.
static void FpkgHmac(const uint8_t *data, unsigned int len, uint8_t hmac[16])
{
    SHA1_CTX ctx;
    uint8_t sha1[20];
    uint8_t buf[64];

    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, sha1);

    memset(buf, 0, 64);
    memcpy(&buf[0], &sha1[4], 8);
    memcpy(&buf[8], &sha1[4], 8);
    memcpy(&buf[16], &sha1[12], 4);
    buf[20] = sha1[16];
    buf[21] = sha1[1];
    buf[22] = sha1[2];
    buf[23] = sha1[3];
    memcpy(&buf[24], &buf[16], 8);

    sha1_init(&ctx);
    sha1_update(&ctx, buf, 64);
    sha1_final(&ctx, sha1);
    memcpy(hmac, sha1, 16);
}

// scePromoterUtilityPromotePkgWithRif requires a sce_sys/package/head.bin
// file (a minimal fake pkg header) or it fails with 0x8010111C.
static bool WriteHeadBin(const std::string &staging_dir, const std::vector<char> &sfo)
{
    std::string title_id = SfoGetString(sfo, "TITLE_ID");
    std::string content_id = SfoGetString(sfo, "CONTENT_ID");

    std::vector<uint8_t> head(tpl_head_bin, tpl_head_bin + tpl_head_bin_len);

    char full_title_id[48];
    snprintf(full_title_id, sizeof(full_title_id), "EP9000-%s_00-0000000000000000", title_id.c_str());
    const std::string &content_id_or_fallback = !content_id.empty() ? content_id : std::string(full_title_id);
    memset(&head[0x30], 0, 48);
    memcpy(&head[0x30], content_id_or_fallback.c_str(), std::min<size_t>(48, content_id_or_fallback.size()));

    uint8_t hmac[16];
    uint32_t len, off, out;

    memcpy(&len, &head[0xD0], 4);
    len = ntohl_local(len);
    FpkgHmac(&head[0], len, hmac);
    memcpy(&head[len], hmac, 16);

    memcpy(&off, &head[0x8], 4);
    off = ntohl_local(off);
    memcpy(&len, &head[0x10], 4);
    len = ntohl_local(len);
    memcpy(&out, &head[0xD4], 4);
    out = ntohl_local(out);
    FpkgHmac(&head[off], len - 64, hmac);
    memcpy(&head[out], hmac, 16);

    memcpy(&len, &head[0xE8], 4);
    len = ntohl_local(len);
    FpkgHmac(&head[0], len, hmac);
    memcpy(&head[len], hmac, 16);

    File::MakeDirs((staging_dir + "sce_sys/package").c_str());
    return File::WriteFile((staging_dir + "sce_sys/package/head.bin").c_str(), head.data(), (SceSSize)head.size());
}

// libretro-thumbnails names images after the exact no-intro game title, with
// a fixed set of characters replaced by "_" - see its own README.
static std::string SanitizeThumbnailName(const std::string &title)
{
    static const std::string bad = "&*/:`<>?\\|";
    std::string out = title;
    for (char &c : out)
    {
        if (bad.find(c) != std::string::npos)
            c = '_';
    }
    return out;
}

static std::string UrlEscapeLocal(const std::string &s)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

struct BoxArt
{
    std::vector<uint8_t> pixels;
    int width = 0, height = 0;
};

// downloads the game's GBA box art from libretro-thumbnails once, decoded
// into memory - callers derive as many differently-sized PNGs from it as
// they need (icon0.png, LiveArea bg.png, startup.png) without re-downloading.
static bool DownloadBoxArt(const std::string &title, BoxArt &out)
{
    std::string url = std::string(LIBRETRO_THUMBNAILS) + "Nintendo%20-%20Game%20Boy%20Advance/" THUMBNAILS_SUBDIR "/" +
                       UrlEscapeLocal(SanitizeThumbnailName(title)) + ".png";

    std::string tmp_path = std::string(CORE_DATA_DIR) + "/tmp_boxart.png";
    bool ok = gNetwork->Download(url.c_str(), tmp_path.c_str());
    blog("DownloadBoxArt: url=%s ok=%d", url.c_str(), (int)ok);
    if (!ok || !File::Exist(tmp_path.c_str()) || File::GetSize(tmp_path.c_str()) == 0)
    {
        File::Remove(tmp_path.c_str());
        return false;
    }

    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_file(&image, tmp_path.c_str()))
    {
        blog("DownloadBoxArt: png_image_begin_read_from_file failed: %s", image.message);
        File::Remove(tmp_path.c_str());
        return false;
    }

    image.format = PNG_FORMAT_RGBA;
    out.pixels.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, out.pixels.data(), 0, nullptr))
    {
        blog("DownloadBoxArt: png_image_finish_read failed: %s", image.message);
        png_image_free(&image);
        File::Remove(tmp_path.c_str());
        return false;
    }
    out.width = (int)image.width;
    out.height = (int)image.height;
    png_image_free(&image);
    File::Remove(tmp_path.c_str());

    blog("DownloadBoxArt: OK, %dx%d", out.width, out.height);
    return true;
}

static std::vector<uint8_t> ResizeToRgb(const BoxArt &src, int dst_w, int dst_h)
{
    std::vector<uint8_t> dst((size_t)dst_w * dst_h * 3);
    for (int y = 0; y < dst_h; y++)
    {
        int sy = y * src.height / dst_h;
        for (int x = 0; x < dst_w; x++)
        {
            int sx = x * src.width / dst_w;
            const uint8_t *s = &src.pixels[(sy * src.width + sx) * 4];
            uint8_t *d = &dst[(y * dst_w + x) * 3];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }
    return dst;
}

// Sony's LiveArea image validator rejects plain truecolor PNGs with
// 0x8010113D - it wants indexed-color PNGs. Quantize onto a fixed 6x6x6
// (216 color) "web safe" palette: simple, always <= 256 colors.
static bool WriteIndexedPng(const std::vector<uint8_t> &rgb, int w, int h, const std::string &dst_path)
{
    png_color palette[216];
    for (int i = 0; i < 216; i++)
    {
        palette[i].red = (png_byte)((i / 36) * 51);
        palette[i].green = (png_byte)(((i / 6) % 6) * 51);
        palette[i].blue = (png_byte)((i % 6) * 51);
    }

    std::vector<uint8_t> indices((size_t)w * h);
    for (size_t i = 0; i < indices.size(); i++)
    {
        int r = (rgb[i * 3 + 0] + 25) / 51;
        int g = (rgb[i * 3 + 1] + 25) / 51;
        int b = (rgb[i * 3 + 2] + 25) / 51;
        if (r > 5)
            r = 5;
        if (g > 5)
            g = 5;
        if (b > 5)
            b = 5;
        indices[i] = (uint8_t)(r * 36 + g * 6 + b);
    }

    FILE *fp = fopen(dst_path.c_str(), "wb");
    if (!fp)
    {
        blog("WriteIndexedPng: fopen failed for %s", dst_path.c_str());
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info)
    {
        blog("WriteIndexedPng: png_create_*_struct failed");
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png)))
    {
        blog("WriteIndexedPng: libpng error while writing %s", dst_path.c_str());
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_PLTE(png, info, palette, 216);
    png_write_info(png, info);

    std::vector<png_bytep> rows(h);
    for (int y = 0; y < h; y++)
        rows[y] = &indices[(size_t)y * w];
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return true;
}

static bool SaveResizedPng(const BoxArt &src, int dst_w, int dst_h, const std::string &dst_path)
{
    std::vector<uint8_t> rgb = ResizeToRgb(src, dst_w, dst_h);
    return WriteIndexedPng(rgb, dst_w, dst_h, dst_path);
}

// File::RemoveAllFiles() is NOT recursive (it only unlinks regular files
// directly inside the given directory, leaving subdirectories and the
// directory itself untouched) - not enough to clear/discard the staging
// tree, which is a full recursive clone of app0:.
static void RemoveDirRecursive(const std::string &path)
{
    SceUID dfd = sceIoDopen(path.c_str());
    if (dfd < 0)
        return;

    SceIoDirent entry;
    while (sceIoDread(dfd, &entry) > 0)
    {
        std::string name(entry.d_name);
        if (name == "." || name == "..")
            continue;

        std::string full = path + "/" + name;
        if (SCE_S_ISDIR(entry.d_stat.st_mode))
            RemoveDirRecursive(full);
        else
            sceIoRemove(full.c_str());
    }
    sceIoDclose(dfd);
    sceIoRmdir(path.c_str());
}

// recursively copies every file this app ships in its own read-only package
// (skins, fonts, database, shaders...) into the new bubble's package,
// skipping the bits we regenerate/replace ourselves (sce_sys, and the
// "bubble/" folder where the target rom gets bundled instead).
static void CopyDirRecursive(const std::string &src_dir, const std::string &dst_dir)
{
    File::MakeDirs(dst_dir.c_str());

    SceUID dfd = sceIoDopen(src_dir.c_str());
    if (dfd < 0)
    {
        blog("CopyDirRecursive: sceIoDopen(%s) failed: 0x%08X", src_dir.c_str(), dfd);
        return;
    }

    SceIoDirent entry;
    while (sceIoDread(dfd, &entry) > 0)
    {
        std::string name(entry.d_name);
        if (name == "." || name == ".." || name == "sce_sys" || name == "bubble")
            continue;

        std::string src_path = src_dir + "/" + name;
        std::string dst_path = dst_dir + "/" + name;
        if (SCE_S_ISDIR(entry.d_stat.st_mode))
        {
            CopyDirRecursive(src_path, dst_path);
        }
        else
        {
            File::CopyFile(src_path.c_str(), dst_path.c_str());
        }
    }
    sceIoDclose(dfd);
}

std::string CreateBubble(const std::string &rom_path, uint32_t rom_crc32)
{
    const std::string app0_root = "app0:";

    if (!File::Exist(rom_path.c_str()))
        return "rom file not found";

    // display title: strip the file extension from the raw filename
    std::string title = File::GetStem(rom_path.c_str());

    // prefer the real no-intro name looked up by the rom's CRC32, so a
    // renamed/mislabeled rom file still gets the correct title and box art
    const char *db_name = nullptr;
    if (rom_crc32 && gRomNameMap->Valid() && gRomNameMap->GetRom(rom_crc32, &db_name) && db_name && db_name[0])
    {
        title = db_name;
    }

    blog("=== CreateBubble: rom=%s title=%s ===", rom_path.c_str(), title.c_str());

    std::string title_id = MakeTitleId(title);
    std::string staging_dir = std::string(CORE_DATA_DIR) + "/bubble_staging/" + title_id;
    blog("staging_dir=%s", staging_dir.c_str());

    RemoveDirRecursive(staging_dir);
    File::MakeDirs(staging_dir.c_str());
    File::MakeDirs((staging_dir + "/sce_sys").c_str());
    File::MakeDirs((staging_dir + "/bubble").c_str());

    // copy every resource this app ships with itself (skins, fonts, shaders,
    // database...) so the cloned bubble is fully self-contained
    blog("copying full app0: package contents...");
    CopyDirRecursive("app0:", staging_dir);

    BoxArt box_art;
    bool have_box_art = DownloadBoxArt(title, box_art);

    std::string icon_dst = staging_dir + "/sce_sys/icon0.png";
    if (have_box_art && SaveResizedPng(box_art, 128, 128, icon_dst))
    {
        blog("using downloaded box art as icon0.png");
    }
    else
    {
        std::string icon_src = app0_root + "sce_sys/icon0.png";
        if (!File::CopyFile(icon_src.c_str(), icon_dst.c_str()))
        {
            blog("FAILED copying fallback icon0.png");
            return "failed to copy icon0.png";
        }
    }

    // LiveArea occasionally showed no bubble icon at all with no error
    // reported here - defend against whatever transient write/copy hiccup
    // caused that by verifying the file actually landed before promoting,
    // and retrying the safe (always-present) fallback copy once if not.
    if (!File::Exist(icon_dst.c_str()) || File::GetSize(icon_dst.c_str()) == 0)
    {
        blog("icon0.png missing or empty after write, retrying fallback copy");
        std::string icon_src = app0_root + "sce_sys/icon0.png";
        if (!File::CopyFile(icon_src.c_str(), icon_dst.c_str()) ||
            !File::Exist(icon_dst.c_str()) || File::GetSize(icon_dst.c_str()) == 0)
        {
            blog("FAILED to produce a valid icon0.png even after retry");
            return "failed to write icon0.png";
        }
    }

    File::MakeDirs((staging_dir + "/sce_sys/livearea/contents").c_str());
    File::CopyFile((app0_root + "sce_sys/livearea/contents/template.xml").c_str(),
                    (staging_dir + "/sce_sys/livearea/contents/template.xml").c_str());

    if (have_box_art)
    {
        bool bg_ok = SaveResizedPng(box_art, 840, 500, staging_dir + "/sce_sys/livearea/contents/bg.png");
        bool startup_ok = SaveResizedPng(box_art, 320, 240, staging_dir + "/sce_sys/livearea/contents/startup.png");
        blog("LiveArea assets from box art: bg=%d startup=%d", (int)bg_ok, (int)startup_ok);
    }
    else
    {
        File::CopyFile((app0_root + "sce_sys/livearea/contents/bg.png").c_str(),
                        (staging_dir + "/sce_sys/livearea/contents/bg.png").c_str());
        File::CopyFile((app0_root + "sce_sys/livearea/contents/startup.png").c_str(),
                        (staging_dir + "/sce_sys/livearea/contents/startup.png").c_str());
    }

    std::string rom_name_only = File::GetName(rom_path.c_str());
    if (!File::CopyFile(rom_path.c_str(), (staging_dir + "/bubble/" + rom_name_only).c_str()))
    {
        blog("FAILED copying rom file");
        return "failed to copy rom file";
    }

    std::vector<char> sfo;
    {
        void *buf = nullptr;
        size_t sfo_size = File::ReadFile((app0_root + "sce_sys/param.sfo").c_str(), &buf);
        if (!sfo_size || !buf)
        {
            blog("FAILED reading param.sfo");
            return "failed to read param.sfo";
        }
        sfo.assign((char *)buf, (char *)buf + sfo_size);
        delete[] (uint8_t *)buf;
    }

    if (!PatchSfo(sfo, title_id, title))
    {
        blog("FAILED patching param.sfo");
        return "failed to patch param.sfo";
    }

    if (!File::WriteFile((staging_dir + "/sce_sys/param.sfo").c_str(), sfo.data(), (SceSSize)sfo.size()))
    {
        blog("FAILED writing param.sfo");
        return "failed to write param.sfo";
    }

    if (!WriteHeadBin(staging_dir + "/", sfo))
    {
        blog("FAILED writing head.bin");
        return "failed to write head.bin";
    }

    // ScePromoterUtil depends on PAF being loaded first.
    uint32_t paf_opt_buf[4] = {0};
    SceSysmoduleOpt *paf_opt = (SceSysmoduleOpt *)paf_opt_buf;
    uint32_t paf_argp[5] = {0x400000, 0xEA60, 0x40000, 0, 0};
    int paf_ret = sceSysmoduleLoadModuleInternalWithArg(
        SCE_SYSMODULE_INTERNAL_PAF, sizeof(paf_argp), paf_argp, paf_opt);
    blog("sceSysmoduleLoadModuleInternalWithArg(PAF) returned 0x%08X", (unsigned)paf_ret);

    int load_ret = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    blog("sceSysmoduleLoadModuleInternal returned 0x%08X", (unsigned)load_ret);

    int init_ret = scePromoterUtilityInit();
    blog("scePromoterUtilityInit returned 0x%08X", (unsigned)init_ret);

    int ret = scePromoterUtilityPromotePkgWithRif((staging_dir + "/").c_str(), 1);
    blog("scePromoterUtilityPromotePkgWithRif returned 0x%08X", (unsigned)ret);

    scePromoterUtilityExit();
    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);

    SceSysmoduleOpt paf_unload_opt = {0, nullptr, {0, 0}};
    sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0, nullptr, &paf_unload_opt);

    RemoveDirRecursive(staging_dir);

    if (ret < 0)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "install failed (0x%08X)", (unsigned)ret);
        return buf;
    }

    return "";
}
