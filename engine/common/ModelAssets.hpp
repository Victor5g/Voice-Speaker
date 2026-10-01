#pragma once
#include "ModelHash.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace lvt {
struct ModelAsset { std::string filename, sha256; std::uint64_t size{}; };
struct ModelBundle {
    std::filesystem::path root;
    std::vector<ModelAsset> files;
    std::string primary;
    std::filesystem::path path(const std::string& relative) const {
        const auto base = std::filesystem::canonical(root);
        const auto result = std::filesystem::weakly_canonical(base / relative);
        auto [a, b] = std::mismatch(base.begin(), base.end(), result.begin(), result.end());
        if (a != base.end() || b == result.end()) throw std::runtime_error("Model path escapes resources");
        return result;
    }
    void verify() const {
        if (files.empty()) throw std::runtime_error("Model assets are absent");
        std::unordered_set<std::string> seen;
        for (const auto& f : files) {
            if (!seen.insert(f.filename).second) throw std::runtime_error("Duplicate model asset");
            auto p = path(f.filename);
            if (!std::filesystem::is_regular_file(p) || std::filesystem::file_size(p) != f.size)
                throw std::runtime_error("Missing or corrupt model asset");
            verifyModelHash(p.string(), f.sha256);
        }
        if (!seen.contains(primary)) throw std::runtime_error("Unverified primary model");
    }
    std::filesystem::path verifiedMember(const std::string& relative) const {
        if (std::none_of(files.begin(), files.end(), [&](const auto& f) { return f.filename == relative; }))
            throw std::runtime_error("Unlisted model auxiliary file");
        return path(relative);
    }
    static ModelBundle fromJSON(const std::filesystem::path& root, const nlohmann::json& entry) {
        ModelBundle bundle{root, {}, entry.at("filename")};
        auto append = [&](const auto& f) { bundle.files.push_back({f.at("filename"), f.at("sha256"), f.at("expectedSize")}); };
        if (entry.contains("files")) for (const auto& f : entry.at("files")) append(f);
        else append(entry);
        return bundle;
    }
};
}
