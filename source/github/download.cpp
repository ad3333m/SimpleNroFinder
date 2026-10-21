#include "github/download.h"

#include <cstdio>
#include <mutex>
#include <set>
#include <sys/stat.h>

#include <curl/curl.h>

#include "github/api.h"
#include "net/async.h"
#include "platform/platform.h"
#include "store/catalog.h"
#include "util/log.h"
#include "util/strings.h"
#include "util/zip.h"

namespace Download {
namespace {

std::mutex g_mu;
std::shared_ptr<Task> g_task;

struct Sink {
    FILE* f = nullptr;
    Task* task = nullptr;
    bool wrote = false;
};

size_t OnBody(char* ptr, size_t size, size_t nmemb, void* user) {
    Sink* s = static_cast<Sink*>(user);
    const size_t n = size * nmemb;
    if (!s->f) return 0;
    if (fwrite(ptr, 1, n, s->f) != n) return 0;
    s->wrote = true;
    s->task->received.fetch_add(n);
    return n;
}

int OnProgress(void* user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    Task* t = static_cast<Task*>(user);
    if (dltotal > 0) t->total.store((uint64_t)dltotal);
    (void)dlnow;
    return t->cancel.load() ? 1 : 0;
}

// mkdir -p for every directory above `path` (and `path` itself if it ends in '/').
void MakeDirs(const std::string& path) {
    const size_t drive = path.find(":/");
    const size_t start = drive == std::string::npos ? 1 : drive + 2;
    for (size_t i = path.find('/', start); i != std::string::npos; i = path.find('/', i + 1))
        mkdir(path.substr(0, i).c_str(), 0777);
}

// Unpacks an App Store zip over the SD root. manifest.install lines look like
// "U: switch/x/x.nro" (update) or "G: config/x.ini" (get once: don't
// overwrite the user's copy). manifest.install and info.json are kept in the
// packages dir so the Homebrew App Store sees the install too.
void InstallPackage(Task& task) {
    const std::vector<ZipEntry> entries = zipList(task.path);
    const std::string root = Platform::SdRoot();
    const std::string meta = Catalog::PackagesDir() + task.pkgName + "/";
    MakeDirs(meta);
    std::set<std::string> keep;
    for (const auto& e : entries) {
        if (e.name != "manifest.install") continue;
        if (zipExtract(task.path, e, meta + e.name)) {
            FILE* f = fopen((meta + e.name).c_str(), "rb");
            char line[1024];
            while (f && fgets(line, sizeof(line), f))
                if (line[0] == 'G' && line[1] == ':') keep.insert(Str::Trim(line + 2));
            if (f) fclose(f);
        }
    }
    int files = 0;
    for (const auto& e : entries) {
        if (e.name.empty() || e.name.back() == '/' || e.name == "manifest.install") continue;
        // Never let a crafted entry escape the SD root.
        if (e.name[0] == '/' || e.name.find("..") != std::string::npos || e.name.find(':') != std::string::npos) continue;
        const bool isMeta = e.name == "info.json";
        const std::string dest = isMeta ? meta + e.name : root + e.name;
        struct stat st;
        if (keep.count(e.name) && stat(dest.c_str(), &st) == 0) continue;
        MakeDirs(dest);
        if (!zipExtract(task.path, e, dest)) {
            task.error = "Couldn't write " + dest;
            return;
        }
        if (!isMeta) files++;
        if (Str::EndsWith(Str::Lower(e.name), ".nro")) task.extracted.push_back(e.name);
    }
    remove(task.path.c_str());
    task.success = files > 0;
    if (!task.success) task.error = "The package was empty.";
    LOG("install: %s -> %d files", task.pkgName.c_str(), files);
}

void Run(std::shared_ptr<Task> task) {
    const std::string partPath = task->path + ".part";
    FILE* f = fopen(partPath.c_str(), "wb");
    if (!f) {
        task->success = false;
        task->error = "Couldn't open " + partPath + " for writing";
        task->done = true;
        LOG("download: fopen failed for %s", partPath.c_str());
        return;
    }
    CURL* c = curl_easy_init();
    if (!c) {
        fclose(f);
        remove(partPath.c_str());
        task->error = "curl init failed";
        task->done = true;
        return;
    }
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "User-Agent: SimpleNroFinder (ad3333m)");
    // GitHub release-asset URLs on api.github.com need the asset Accept header,
    // but browser_download_url is served from github-releases.githubusercontent.com,
    // which doesn't need it. Add both headers; the second is ignored by raw hosts.
    hdrs = curl_slist_append(hdrs, "Accept: application/octet-stream, */*;q=0.5");
    if (Api::HaveToken()) {
        std::string h = "Authorization: Bearer " + Api::Token();
        hdrs = curl_slist_append(hdrs, h.c_str());
    }

    Sink sink{f, task.get(), false};
    curl_easy_setopt(c, CURLOPT_URL, task->url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);  // one byte/s floor
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);  // for a minute
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, OnProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, task.get());

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    fclose(f);

    const bool wasCancelled = task->cancel.load();
    const bool httpOk = code >= 200 && code < 300;
    if (rc == CURLE_OK && httpOk && sink.wrote) {
        remove(task->path.c_str());  // overwrite
        if (rename(partPath.c_str(), task->path.c_str()) != 0) {
            task->success = false;
            task->error = "Couldn't move " + partPath + " to " + task->path;
            remove(partPath.c_str());
        } else {
            // If this was a zip, scan it for .nro files and extract each one
            // into destDir. The zip itself is deleted after a successful extract
            // so the SD card doesn't fill up with the originals.
            if (!task->pkgName.empty()) {
                task->stage = 1;
                InstallPackage(*task);
            } else if (Str::EndsWith(Str::Lower(task->path), ".zip")) {
                task->stage = 1;
                std::vector<ZipEntry> entries = zipList(task->path);
                int extracted = 0;
                for (const auto& e : entries) {
                    if (!Str::EndsWith(Str::Lower(e.name), ".nro")) continue;
                    // Keep only the file's basename so folder-structured zips
                    // (foo-1.2/foo.nro) still land directly in destDir.
                    std::string base = e.name;
                    size_t slash = base.find_last_of("/\\");
                    if (slash != std::string::npos) base = base.substr(slash + 1);
                    if (base.empty()) continue;
                    std::string dest = task->destDir + base;
                    if (zipExtract(task->path, e, dest)) {
                        extracted++;
                        task->extracted.push_back(base);
                        LOG("download: extracted %s from zip", dest.c_str());
                    } else {
                        LOG("download: couldn't extract %s from zip", e.name.c_str());
                    }
                }
                if (extracted > 0) {
                    remove(task->path.c_str());  // keep only the nro(s)
                    task->success = true;
                } else {
                    // No .nro inside — leave the zip in place and tell the user.
                    task->success = false;
                    task->error = "No .nro files were found inside the zip.";
                }
            } else {
                task->success = true;
                LOG("download: saved %s (%llu bytes)", task->path.c_str(), (unsigned long long)task->received.load());
            }
        }
    } else {
        remove(partPath.c_str());
        task->success = false;
        if (wasCancelled) task->error = "Cancelled";
        else if (rc != CURLE_OK) task->error = curl_easy_strerror(rc);
        else task->error = "HTTP " + std::to_string(code);
    }
    task->done = true;
}

}  // namespace

std::shared_ptr<Task> Start(const std::string& url, const std::string& path, const std::string& displayName,
                            uint64_t expectedSize, const std::string& destDir) {
    CancelCurrent();
    auto task = std::make_shared<Task>();
    task->url = url;
    task->path = path;
    task->displayName = displayName;
    task->expectedSize = expectedSize;
    task->total = expectedSize;
    if (!destDir.empty()) {
        task->destDir = destDir;
    } else {
        // Default to the parent directory of the destination path.
        size_t slash = path.find_last_of("/\\");
        task->destDir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_task = task;
    }
    Async::Run(Async::Pool::Images, [task] { Run(task); });
    return task;
}

std::shared_ptr<Task> StartPackage(const std::string& url, const std::string& name, const std::string& title,
                                   const std::string& version, uint64_t expectedSize) {
    CancelCurrent();
    auto task = std::make_shared<Task>();
    task->url = url;
    task->path = Platform::DataDir() + "package.zip";
    task->displayName = title;
    task->destDir = Platform::SdRoot();
    task->expectedSize = expectedSize;
    task->total = expectedSize;
    task->pkgName = name;
    task->pkgVersion = version;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_task = task;
    }
    Async::Run(Async::Pool::Images, [task] { Run(task); });
    return task;
}

std::shared_ptr<Task> Current() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_task;
}

void CancelCurrent() {
    std::shared_ptr<Task> t;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        t = g_task;
    }
    if (t && !t->done) t->cancel = true;
}

void Shutdown() { CancelCurrent(); }

}  // namespace Download
