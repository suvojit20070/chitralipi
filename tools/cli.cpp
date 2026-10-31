// chitralipi command line tool.
// SPDX-License-Identifier: MIT
#include "chitralipi/chitralipi.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifdef CHL_HAVE_STB
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#endif

namespace {

const char* kUsage =
    "Chitralipi %s - image to text and back\n"
    "\n"
    "Usage:\n"
    "  chitralipi encode <image> [-o out.txt] [-q 1-100] [--lossless] [--max-chars N]\n"
    "                    [--no-resize] [--ascii] [--levels 0-15] [--step N]\n"
    "  chitralipi decode <in.txt> -o <image>\n"
    "  chitralipi pack   <any file> [-o out.txt] [--ascii]     (exact bytes, no compression)\n"
    "  chitralipi unpack <in.txt> -o <file>\n"
    "  chitralipi info   <in.txt>\n"
    "\n"
    "Images: %s\n"
    "Without -o the text goes to standard output.\n";

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "chitralipi: %s\n", msg.c_str());
    std::exit(1);
}

void check(int32_t st, const char* what) {
    if (st != CHL_OK) die(std::string(what) + ": " + chl_status_string(st));
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) die("cannot open " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void write_file(const std::string& path, const void* data, size_t n) {
    std::ofstream f(path, std::ios::binary);
    if (!f) die("cannot write " + path);
    f.write(static_cast<const char*>(data), std::streamsize(n));
    if (!f) die("cannot write " + path);
}

std::string lower_ext(const std::string& path) {
    size_t dot = path.find_last_of('.');
    std::string e = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

struct Pixels {
    std::vector<uint8_t> px;
    uint32_t w = 0, h = 0;
    int channels = 0;
};

// ---- PNM / PAM (always available) ----------------------------------------------
std::string pnm_token(std::istream& in) {
    std::string t;
    char c;
    while (in.get(c)) {
        if (c == '#') {
            std::string line;
            std::getline(in, line);
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!t.empty()) break;
            continue;
        }
        t.push_back(c);
    }
    return t;
}

bool read_pnm(const std::vector<uint8_t>& file, Pixels& out) {
    if (file.size() < 2 || file[0] != 'P') return false;
    std::string s(file.begin(), file.end());
    std::istringstream in(s);
    std::string magic = pnm_token(in);
    uint64_t w = 0, h = 0, maxv = 0;
    int depth = 0;
    if (magic == "P5" || magic == "P6") {
        w = std::stoull(pnm_token(in));
        h = std::stoull(pnm_token(in));
        maxv = std::stoull(pnm_token(in));
        depth = magic == "P5" ? 1 : 3;
    } else if (magic == "P7") {
        for (;;) {
            std::string t = pnm_token(in);
            if (t.empty()) return false;
            if (t == "ENDHDR") break;
            if (t == "WIDTH") w = std::stoull(pnm_token(in));
            else if (t == "HEIGHT") h = std::stoull(pnm_token(in));
            else if (t == "DEPTH") depth = std::stoi(pnm_token(in));
            else if (t == "MAXVAL") maxv = std::stoull(pnm_token(in));
            else if (t == "TUPLTYPE") pnm_token(in);
        }
    } else {
        return false;
    }
    if (maxv != 255 || w == 0 || h == 0 || depth < 1 || depth > 4) die("only 8-bit PNM/PAM images are supported");
    size_t start = size_t(in.tellg());
    size_t need = size_t(w * h * uint64_t(depth));
    if (file.size() < start + need) die("image file is truncated");
    out.px.assign(file.begin() + std::ptrdiff_t(start), file.begin() + std::ptrdiff_t(start + need));
    out.w = uint32_t(w);
    out.h = uint32_t(h);
    out.channels = depth;
    return true;
}

void write_pnm(const std::string& path, const Pixels& p) {
    std::string hdr;
    if (p.channels == 1) hdr = "P5\n" + std::to_string(p.w) + " " + std::to_string(p.h) + "\n255\n";
    else if (p.channels == 3) hdr = "P6\n" + std::to_string(p.w) + " " + std::to_string(p.h) + "\n255\n";
    else
        hdr = "P7\nWIDTH " + std::to_string(p.w) + "\nHEIGHT " + std::to_string(p.h) + "\nDEPTH " +
              std::to_string(p.channels) + "\nMAXVAL 255\nTUPLTYPE " +
              (p.channels == 2 ? "GRAYSCALE_ALPHA" : "RGB_ALPHA") + "\nENDHDR\n";
    std::vector<uint8_t> out(hdr.begin(), hdr.end());
    out.insert(out.end(), p.px.begin(), p.px.end());
    write_file(path, out.data(), out.size());
}

Pixels load_image(const std::string& path) {
    std::vector<uint8_t> file = read_file(path);
    Pixels p;
    if (read_pnm(file, p)) return p;
#ifdef CHL_HAVE_STB
    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &c, 0);
    if (!data) die("cannot read image " + path + ": " + stbi_failure_reason());
    p.w = uint32_t(w);
    p.h = uint32_t(h);
    p.channels = c;
    p.px.assign(data, data + size_t(w) * size_t(h) * size_t(c));
    stbi_image_free(data);
    return p;
#else
    die("this build reads only PPM/PGM/PAM. Rebuild with -DCHITRALIPI_FETCH_STB=ON for PNG/JPEG");
#endif
}

void save_image(const std::string& path, const Pixels& p) {
    std::string e = lower_ext(path);
    if (e == "ppm" || e == "pgm" || e == "pam" || e == "pnm") return write_pnm(path, p);
#ifdef CHL_HAVE_STB
    int w = int(p.w), h = int(p.h), c = p.channels, ok = 0;
    if (e == "jpg" || e == "jpeg") ok = stbi_write_jpg(path.c_str(), w, h, c, p.px.data(), 95);
    else if (e == "bmp") ok = stbi_write_bmp(path.c_str(), w, h, c, p.px.data());
    else if (e == "tga") ok = stbi_write_tga(path.c_str(), w, h, c, p.px.data());
    else ok = stbi_write_png(path.c_str(), w, h, c, p.px.data(), w * c);
    if (!ok) die("cannot write " + path);
#else
    die("this build writes only .ppm/.pgm/.pam. Rebuild with -DCHITRALIPI_FETCH_STB=ON for PNG/JPEG");
#endif
}

std::string read_text(const std::string& path) {
    std::vector<uint8_t> f = read_file(path);
    return std::string(f.begin(), f.end());
}

void emit_text(const std::string& out_path, const char* text, size_t len) {
    if (out_path.empty()) {
        std::fwrite(text, 1, len, stdout);
        std::fputc('\n', stdout);
    } else {
        write_file(out_path, text, len);
    }
}

const char* kind_name(uint32_t k) {
    static const char* names[] = {"unknown", "JPEG", "PNG", "WebP", "AVIF", "GIF", "HEIF"};
    return k < 7 ? names[k] : "unknown";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef CHL_HAVE_STB
    const char* formats = "PNG, JPEG, BMP, TGA, GIF (read), PPM/PGM/PAM";
#else
    const char* formats = "PPM/PGM/PAM only (build with -DCHITRALIPI_FETCH_STB=ON for PNG/JPEG)";
#endif
    if (argc < 3) {
        std::fprintf(stderr, kUsage, chl_version(), formats);
        return argc == 2 && (!std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h")) ? 0 : 1;
    }
    std::string cmd = argv[1], input = argv[2], out;
    chl_options opt;
    chl_options_default(&opt);
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) die("missing value after " + a);
            return argv[++i];
        };
        if (a == "-o") out = next();
        else if (a == "-q") opt.quality = std::atoi(next().c_str());
        else if (a == "--lossless") opt.quality = 100;
        else if (a == "--max-chars") opt.max_chars = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--no-resize") opt.allow_resize = 0;
        else if (a == "--ascii") opt.text_mode = CHL_TEXT_ASCII;
        else if (a == "--levels") opt.levels = std::atoi(next().c_str());
        else if (a == "--step") opt.base_step = std::atoi(next().c_str());
        else die("unknown option " + a);
    }

    if (cmd == "encode") {
        Pixels p = load_image(input);
        char* text = nullptr;
        size_t len = 0;
        check(chl_encode(p.px.data(), p.w, p.h, p.channels, &opt, &text, &len), "encode");
        emit_text(out, text, len);
        chl_info info;
        if (chl_get_info(text, len, &info) == CHL_OK)
            std::fprintf(stderr, "%ux%u -> %ux%u, step %u, %llu characters (%zu UTF-8 bytes)\n", p.w, p.h,
                         info.width, info.height, info.base_step, (unsigned long long)info.chars, len);
        chl_free(text);
    } else if (cmd == "decode") {
        if (out.empty()) die("decode needs -o <image>");
        std::string text = read_text(input);
        Pixels p;
        uint8_t* px = nullptr;
        int32_t ch = 0;
        check(chl_decode(text.data(), text.size(), &px, &p.w, &p.h, &ch), "decode");
        p.channels = ch;
        p.px.assign(px, px + size_t(p.w) * p.h * size_t(ch));
        chl_free(px);
        save_image(out, p);
        std::fprintf(stderr, "%ux%u, %d channels -> %s\n", p.w, p.h, ch, out.c_str());
    } else if (cmd == "pack") {
        std::vector<uint8_t> file = read_file(input);
        char* text = nullptr;
        size_t len = 0;
        check(chl_pack(file.data(), file.size(), opt.text_mode, &text, &len), "pack");
        emit_text(out, text, len);
        std::fprintf(stderr, "%zu bytes -> %llu characters\n", file.size(),
                     (unsigned long long)chl_char_count(text, len));
        chl_free(text);
    } else if (cmd == "unpack") {
        if (out.empty()) die("unpack needs -o <file>");
        std::string text = read_text(input);
        uint8_t* bytes = nullptr;
        size_t n = 0;
        uint32_t kind = 0;
        check(chl_unpack(text.data(), text.size(), &bytes, &n, &kind), "unpack");
        write_file(out, bytes, n);
        chl_free(bytes);
        std::fprintf(stderr, "%zu bytes (%s) -> %s\n", n, kind_name(kind), out.c_str());
    } else if (cmd == "info") {
        std::string text = read_text(input);
        chl_info info;
        check(chl_get_info(text.data(), text.size(), &info), "info");
        std::printf("version     %d\n", info.version);
        if (info.mode == 1) {
            std::printf("type        image\nsize        %ux%u\nchannels    %d\nstep        %u%s\nlevels      %d\n",
                        info.width, info.height, info.channels, info.base_step,
                        info.base_step == 1 ? " (lossless)" : "", info.levels);
        } else {
            std::printf("type        packed file (%s)\n", kind_name(info.file_kind));
        }
        std::printf("data        %llu bytes\npackage     %llu bytes\ncharacters  %llu\n",
                    (unsigned long long)info.data_bytes, (unsigned long long)info.package_bytes,
                    (unsigned long long)info.chars);
    } else {
        std::fprintf(stderr, kUsage, chl_version(), formats);
        return 1;
    }
    return 0;
}
