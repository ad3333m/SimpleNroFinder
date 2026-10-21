#include "store/catalog.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>

#include "net/async.h"
#include "net/http.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"
#include "util/strings.h"

namespace {

const char* kCdn = "https://switch.cdn.fortheusers.org/";

std::string ReadFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

std::map<std::string, std::string> ReadInstalled() {
    std::map<std::string, std::string> out;
    const std::string dir = Catalog::PackagesDir();
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        auto doc = J::Parse(ReadFile(dir + name + "/info.json"));
        if (yyjson_val* root = J::Root(doc)) out[name] = J::S(root, "version");
    }
    closedir(d);
    return out;
}

}  // namespace

std::string Package::IconUrl() const { return std::string(kCdn) + "packages/" + name + "/icon.png"; }
std::string Package::ZipUrl() const { return std::string(kCdn) + "zips/" + name + ".zip"; }

namespace Catalog {

std::string PackagesDir() { return Platform::SdRoot() + "switch/appstore/.get/packages/"; }

void Fetch(Fn cb) {
    Async::Run(Async::Pool::Api, [cb = std::move(cb)] {
        Http::Request req;
        req.url = std::string(kCdn) + "repo.json";
        req.headers = {"User-Agent: SimpleNroFinder (ad3333m)"};
        req.timeoutMs = 30000;
        Http::Response r = Http::Perform(req);
        auto installed = ReadInstalled();
        auto doc = J::Parse(r.body);
        yyjson_val* pkgs = J::Get(J::Root(doc), "packages");
        if (!r.Ok() || !yyjson_is_arr(pkgs)) {
            std::string err = !r.error.empty() ? r.error : "Catalogue unavailable (HTTP " + std::to_string(r.status) + ")";
            Async::Post([cb, err] { cb(false, {}, {}, err); });
            return;
        }
        std::vector<Package> list;
        size_t i, n;
        yyjson_val* v;
        yyjson_arr_foreach(pkgs, i, n, v) {
            Package p;
            p.name = J::S(v, "name");
            if (p.name.empty()) continue;
            p.title = J::S(v, "title");
            if (p.title.empty()) p.title = p.name;
            p.author = J::S(v, "author");
            p.category = J::S(v, "category");
            p.version = J::S(v, "version");
            p.description = J::S(v, "description");
            p.details = Str::Replace(J::S(v, "details"), "\\n", "\n");  // stored with escaped newlines
            p.binary = J::S(v, "binary");
            p.license = J::S(v, "license");
            p.updated = J::S(v, "updated");
            p.downloads = J::I(v, "app_dls");
            p.size = (uint64_t)J::I(v, "filesize") * 1024;  // repo.json counts KB
            list.push_back(std::move(p));
        }
        std::stable_sort(list.begin(), list.end(),
                         [](const Package& a, const Package& b) { return a.downloads > b.downloads; });
        LOG("catalog: %zu packages, %zu installed", list.size(), installed.size());
        Async::Post([cb, list = std::move(list), installed = std::move(installed)] { cb(true, list, installed, ""); });
    });
}

}  // namespace Catalog
