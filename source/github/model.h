#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Lightweight views of the GitHub JSON shapes we actually read. Keep these as
// plain structs: no inheritance, no pointers, so lists are cheap to copy around.
struct Repo {
    int64_t id = 0;
    std::string owner;      // "ad3333m"
    std::string name;       // "switchcord"
    std::string fullName;   // "ad3333m/switchcord"
    std::string description;
    std::string defaultBranch = "main";
    std::string language;
    int stars = 0, forks = 0;
    bool isPrivate = false;
    bool fork = false;
    bool archived = false;
    std::string ownerAvatar;  // url
    int64_t pushedAtSec = 0;
};

struct User {
    int64_t id = 0;
    std::string login;
    std::string name;
    std::string avatar;
    std::string bio;
    int publicRepos = 0, followers = 0, following = 0;
};

struct Asset {
    int64_t id = 0;
    std::string name;
    std::string url;         // browser_download_url
    uint64_t size = 0;
    int downloads = 0;
    std::string contentType;
};

struct Release {
    int64_t id = 0;
    std::string tag;
    std::string name;        // display name, may equal tag
    std::string body;        // markdown; we show first lines only
    bool prerelease = false;
    bool draft = false;
    int64_t publishedAtSec = 0;
    std::vector<Asset> assets;
    std::string tarball;     // url
    std::string zipball;     // url
};

struct TreeEntry {
    enum Kind { Dir, File, Submodule, Symlink } kind = File;
    std::string name;        // last path segment
    std::string path;        // full path within repo
    std::string sha;
    uint64_t size = 0;
    // raw.githubusercontent URL for files; empty for directories.
    std::string downloadUrl;
};

struct Branch {
    std::string name;
    std::string sha;
    bool protected_ = false;
};
