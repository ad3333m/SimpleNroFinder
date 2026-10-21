#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// The Homebrew App Store catalogue (switch.cdn.fortheusers.org/repo.json):
// ~500 curated Switch homebrew packages with download counts and categories.
struct Package {
    std::string name;      // package id, e.g. "Goldleaf"
    std::string title;
    std::string author;
    std::string category;  // game, emu, tool, advanced, theme, ...
    std::string version;
    std::string description;
    std::string details;
    std::string binary;    // "/switch/x/x.nro", or "none" for sysmodules/overlays
    std::string license;
    std::string updated;   // dd/mm/yyyy
    int64_t downloads = 0;
    uint64_t size = 0;     // zip bytes
    std::string IconUrl() const;
    std::string ZipUrl() const;
};

namespace Catalog {

// Where installed packages are recorded, shared with the Homebrew App Store
// so both apps agree on what's installed.
std::string PackagesDir();  // sdmc:/switch/appstore/.get/packages/

// Fetches repo.json, sorted by downloads (most popular first). `installed`
// maps package name -> installed version, read from PackagesDir().
using Fn = std::function<void(bool ok, std::vector<Package> list, std::map<std::string, std::string> installed,
                              std::string err)>;
void Fetch(Fn cb);

}  // namespace Catalog
