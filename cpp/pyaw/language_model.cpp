#include "language_model.hpp"

#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "unicode.hpp"
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pyaw {

std::unique_ptr<MappedFile> MappedFile::open(const std::string& path) {
    std::unique_ptr<MappedFile> f(new MappedFile());
#ifdef _WIN32
    std::u16string wide = utf8_to_utf16(path);
    HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(wide.c_str()), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart == 0) { CloseHandle(file); return nullptr; }
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) { CloseHandle(file); return nullptr; }
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) { CloseHandle(mapping); CloseHandle(file); return nullptr; }
    f->file_ = file;
    f->mapping_ = mapping;
    f->data_ = static_cast<const unsigned char*>(view);
    f->size_ = static_cast<size_t>(size.QuadPart);
#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return nullptr;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) { ::close(fd); return nullptr; }
    void* p = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED) return nullptr;
    f->data_ = static_cast<const unsigned char*>(p);
    f->size_ = static_cast<size_t>(st.st_size);
#endif
    return f;
}

MappedFile::~MappedFile() {
#ifdef _WIN32
    if (data_) UnmapViewOfFile(data_);
    if (mapping_) CloseHandle(mapping_);
    if (file_) CloseHandle(file_);
#else
    if (data_) munmap(const_cast<unsigned char*>(data_), size_);
#endif
}

namespace {

constexpr uint32_t kMagic = 0x4D4C594D;  // "MYLM"

template <typename K>
long search(const K* keys, size_t count, K key) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = (lo + hi) >> 1;
        K k = keys[mid];
        if (k < key) lo = mid + 1;
        else if (k > key) hi = mid;
        else return static_cast<long>(mid);
    }
    return -1;
}

}  // namespace

std::unique_ptr<LanguageModel> LanguageModel::load(const std::string& path) {
    auto file = MappedFile::open(path);
    if (!file || file->size() < 32) return nullptr;
    const unsigned char* base = file->data();
    auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, base + at, 4); return v; };
    if (u32(0) != kMagic || u32(4) != 1) return nullptr;
    const size_t V = u32(8), B = u32(12), T = u32(16);
    size_t offset = 32;
    bool ok = true;
    auto align = [&] { offset = (offset + 7) & ~static_cast<size_t>(7); };
    auto take = [&](size_t bytes) -> const unsigned char* {
        align();
        if (offset + bytes > file->size()) { ok = false; return nullptr; }
        const unsigned char* p = base + offset;
        offset += bytes;
        return p;
    };
    std::unique_ptr<LanguageModel> lm(new LanguageModel());
    const unsigned char* offsetsBytes = take(4 * (V + 1));
    if (!ok) return nullptr;
    const size_t textStart = offset;
    std::vector<uint32_t> offsets(V + 1);
    std::memcpy(offsets.data(), offsetsBytes, 4 * (V + 1));
    if (textStart + offsets[V] > file->size()) return nullptr;
    lm->vocab_.reserve(V);
    for (size_t i = 0; i < V; ++i) {
        lm->vocab_.emplace_back(reinterpret_cast<const char*>(base + textStart + offsets[i]), offsets[i + 1] - offsets[i]);
        lm->index_.emplace(lm->vocab_.back(), static_cast<Token>(i));
    }
    offset = textStart + offsets[V];
    lm->uniLogProb_ = reinterpret_cast<const float*>(take(4 * V));
    lm->uniBackoff_ = reinterpret_cast<const float*>(take(4 * V));
    lm->biKeys_ = reinterpret_cast<const uint32_t*>(take(4 * B));
    lm->biLogProb_ = reinterpret_cast<const float*>(take(4 * B));
    lm->biBackoff_ = reinterpret_cast<const float*>(take(4 * B));
    lm->triKeys_ = reinterpret_cast<const uint64_t*>(take(8 * T));
    lm->triLogProb_ = reinterpret_cast<const float*>(take(4 * T));
    if (!ok) return nullptr;
    lm->biCount_ = B;
    lm->triCount_ = T;
    lm->file_ = std::move(file);
    return lm;
}

int LanguageModel::token(const std::string& syllable) const {
    auto it = index_.find(syllable);
    return it == index_.end() ? -1 : it->second;
}

float LanguageModel::bigramLogProb(Token w, Token v) const {
    long i = search<uint32_t>(biKeys_, biCount_, static_cast<uint32_t>(v) << 16 | w);
    if (i >= 0) return biLogProb_[i];
    return uniBackoff_[v] + uniLogProb_[w];
}

float LanguageModel::logProb(Token w, Token u, Token v) const {
    long i = search<uint64_t>(triKeys_, triCount_, static_cast<uint64_t>(u) << 32 | static_cast<uint64_t>(v) << 16 | w);
    if (i >= 0) return triLogProb_[i];
    float backoff = 0;
    long j = search<uint32_t>(biKeys_, biCount_, static_cast<uint32_t>(u) << 16 | v);
    if (j >= 0) backoff = biBackoff_[j];
    return backoff + bigramLogProb(w, v);
}

}  // namespace pyaw
