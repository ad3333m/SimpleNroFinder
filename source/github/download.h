#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

// Streams a download to disk with progress updates on the main thread.
// Writes to <path>.part, then renames to <path> on success. One active download
// at a time: starting another cancels the first.
namespace Download {

struct Task {
    std::string url;
    std::string path;          // final destination, e.g. "sdmc:/switch/foo.nro"
    std::string displayName;   // shown in the dialog
    std::string destDir;       // where extracted .nro files go when `path` is a zip
    uint64_t expectedSize = 0; // 0 = unknown
    std::atomic<uint64_t> received{0};
    std::atomic<uint64_t> total{0};
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::atomic<int> stage{0}; // 0 = downloading, 1 = extracting
    bool success = false;      // valid once done=true
    std::string error;         // valid once done=true (empty on success)
    // When the download was a zip, this lists the .nro basenames extracted into
    // destDir. On a single-file nro download, this is empty.
    std::vector<std::string> extracted;
    // Set for App Store packages: the zip is unpacked over the SD root.
    std::string pkgName, pkgVersion;
};

// Begins the download. The returned task survives until the next Start() call
// or Shutdown(). Pass nullptr to cancel any in-flight download.
// If `path` ends in ".zip", the file is scanned after the transfer finishes and
// every `.nro` inside is extracted to `destDir` (defaults to the parent of path).
std::shared_ptr<Task> Start(const std::string& url, const std::string& path, const std::string& displayName,
                            uint64_t expectedSize = 0, const std::string& destDir = "");
// Downloads an App Store package zip and unpacks every file in it to the SD
// root (switch/<app>/..., atmosphere/..., config/...), then records it under
// Catalog::PackagesDir() the way the Homebrew App Store does.
std::shared_ptr<Task> StartPackage(const std::string& url, const std::string& name, const std::string& title,
                                   const std::string& version, uint64_t expectedSize);
std::shared_ptr<Task> Current();
void CancelCurrent();
void Shutdown();

}  // namespace Download
