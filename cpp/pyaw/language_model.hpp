// Reader for model.bin (trigram syllable model, see Sources/PyawCore/LanguageModel.swift).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pyaw {

using Token = uint16_t;

/// Read-only memory-mapped file (shared between processes on Windows and POSIX).
class MappedFile {
public:
    static std::unique_ptr<MappedFile> open(const std::string& path);
    ~MappedFile();
    const unsigned char* data() const { return data_; }
    size_t size() const { return size_; }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

private:
    MappedFile() = default;
    const unsigned char* data_ = nullptr;
    size_t size_ = 0;
#ifdef _WIN32
    void* file_ = nullptr;
    void* mapping_ = nullptr;
#endif
};

class LanguageModel {
public:
    static constexpr Token bos = 0, eos = 1, unk = 2;

    /// Returns nullptr if the file is missing or malformed.
    static std::unique_ptr<LanguageModel> load(const std::string& path);

    const std::vector<std::string>& vocab() const { return vocab_; }
    size_t vocabSize() const { return vocab_.size(); }
    size_t bigramCount() const { return biCount_; }
    size_t trigramCount() const { return triCount_; }
    /// Token for a syllable (UTF-8), or -1.
    int token(const std::string& syllable) const;

    float unigramLogProb(Token w) const { return uniLogProb_[w]; }
    float bigramLogProb(Token w, Token v) const;
    /// ln P(w | u v)
    float logProb(Token w, Token u, Token v) const;

private:
    std::unique_ptr<MappedFile> file_;
    std::vector<std::string> vocab_;
    std::unordered_map<std::string, Token> index_;
    const float* uniLogProb_ = nullptr;
    const float* uniBackoff_ = nullptr;
    const uint32_t* biKeys_ = nullptr;
    const float* biLogProb_ = nullptr;
    const float* biBackoff_ = nullptr;
    const uint64_t* triKeys_ = nullptr;
    const float* triLogProb_ = nullptr;
    size_t biCount_ = 0, triCount_ = 0;
};

}  // namespace pyaw
