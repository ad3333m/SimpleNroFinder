#include "github/api.h"

#include <atomic>
#include <mutex>

#include "net/async.h"
#include "net/http.h"
#include "util/json.h"
#include "util/log.h"
#include "util/strings.h"
#include "yyjson.h"

namespace Api {
namespace {

std::mutex g_mu;
std::string g_token;

std::vector<std::string> StdHeaders() {
    std::vector<std::string> h{"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28",
                               "User-Agent: SimpleNroFinder (ad3333m)"};
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_token.empty()) h.push_back("Authorization: Bearer " + g_token);
    return h;
}

Http::Response Call(const std::string& method, const std::string& url, const std::string& body = "",
                    long timeoutMs = 20000) {
    Http::Request req;
    req.method = method;
    req.url = url;
    req.body = body;
    req.timeoutMs = timeoutMs;
    req.headers = StdHeaders();
    if (!body.empty()) req.headers.push_back("Content-Type: application/json");
    return Http::Perform(req);
}

// Pulls the "message" from an error JSON ("Not Found", "Bad credentials", ...).
std::string ErrorMessage(const Http::Response& r) {
    if (!r.error.empty()) return r.error;
    auto d = J::Parse(r.body);
    if (yyjson_val* root = J::Root(d)) {
        std::string m = J::S(root, "message");
        if (!m.empty()) return m;
    }
    return "HTTP " + std::to_string(r.status);
}

// days-from-civil, Howard Hinnant: converts a UTC Y/M/D into days since the
// Unix epoch without touching the libc TZ state. devkitA64's newlib has no
// timegm, and we want a UTC-only parse anyway.
int64_t DaysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + (int64_t)doe - 719468LL;
}

int64_t ParseIso(const std::string& s) {
    if (s.size() < 19) return 0;
    int y, mo, d, h, mi, se;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    int64_t days = DaysFromCivil(y, (unsigned)mo, (unsigned)d);
    return days * 86400 + (int64_t)h * 3600 + (int64_t)mi * 60 + se;
}

Repo ParseRepo(yyjson_val* v) {
    Repo r;
    r.id = J::I(v, "id");
    r.fullName = J::S(v, "full_name");
    r.name = J::S(v, "name");
    r.description = J::S(v, "description");
    r.defaultBranch = J::S(v, "default_branch");
    if (r.defaultBranch.empty()) r.defaultBranch = "main";
    r.language = J::S(v, "language");
    r.stars = (int)J::I(v, "stargazers_count");
    r.forks = (int)J::I(v, "forks_count");
    r.isPrivate = J::Bool(v, "private");
    r.fork = J::Bool(v, "fork");
    r.archived = J::Bool(v, "archived");
    r.pushedAtSec = ParseIso(J::S(v, "pushed_at"));
    if (yyjson_val* o = J::Get(v, "owner")) {
        r.owner = J::S(o, "login");
        r.ownerAvatar = J::S(o, "avatar_url");
    }
    if (r.owner.empty()) {
        size_t slash = r.fullName.find('/');
        if (slash != std::string::npos) r.owner = r.fullName.substr(0, slash);
    }
    return r;
}

User ParseUser(yyjson_val* v) {
    User u;
    u.id = J::I(v, "id");
    u.login = J::S(v, "login");
    u.name = J::S(v, "name");
    u.avatar = J::S(v, "avatar_url");
    u.bio = J::S(v, "bio");
    u.publicRepos = (int)J::I(v, "public_repos");
    u.followers = (int)J::I(v, "followers");
    u.following = (int)J::I(v, "following");
    return u;
}

Asset ParseAsset(yyjson_val* v) {
    Asset a;
    a.id = J::I(v, "id");
    a.name = J::S(v, "name");
    a.url = J::S(v, "browser_download_url");
    a.size = (uint64_t)J::I(v, "size");
    a.downloads = (int)J::I(v, "download_count");
    a.contentType = J::S(v, "content_type");
    return a;
}

Release ParseRelease(yyjson_val* v) {
    Release rel;
    rel.id = J::I(v, "id");
    rel.tag = J::S(v, "tag_name");
    rel.name = J::S(v, "name");
    if (rel.name.empty()) rel.name = rel.tag;
    rel.body = J::S(v, "body");
    rel.prerelease = J::Bool(v, "prerelease");
    rel.draft = J::Bool(v, "draft");
    rel.publishedAtSec = ParseIso(J::S(v, "published_at"));
    rel.tarball = J::S(v, "tarball_url");
    rel.zipball = J::S(v, "zipball_url");
    for (yyjson_val* a : J::Arr(v, "assets")) rel.assets.push_back(ParseAsset(a));
    return rel;
}

TreeEntry ParseEntry(yyjson_val* v) {
    TreeEntry e;
    e.name = J::S(v, "name");
    e.path = J::S(v, "path");
    e.sha = J::S(v, "sha");
    e.size = (uint64_t)J::I(v, "size");
    std::string type = J::S(v, "type");
    if (type == "dir") e.kind = TreeEntry::Dir;
    else if (type == "submodule") e.kind = TreeEntry::Submodule;
    else if (type == "symlink") e.kind = TreeEntry::Symlink;
    else e.kind = TreeEntry::File;
    e.downloadUrl = J::S(v, "download_url");
    return e;
}

Branch ParseBranch(yyjson_val* v) {
    Branch b;
    b.name = J::S(v, "name");
    if (yyjson_val* c = J::Get(v, "commit")) b.sha = J::S(c, "sha");
    b.protected_ = J::Bool(v, "protected");
    return b;
}

}  // namespace

void SetToken(const std::string& t) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_token = t;
}
const std::string& Token() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_token;
}
bool HaveToken() {
    std::lock_guard<std::mutex> lk(g_mu);
    return !g_token.empty();
}

void FetchMe(const std::string& token, MeFn cb) {
    Async::Run(Async::Pool::Api, [token, cb = std::move(cb)] {
        Http::Request req;
        req.url = "https://api.github.com/user";
        req.headers = {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28",
                       "User-Agent: SimpleNroFinder (ad3333m)", "Authorization: Bearer " + token};
        req.timeoutMs = 15000;
        Http::Response r = Http::Perform(req);
        if (!r.Ok()) {
            std::string err = ErrorMessage(r);
            Async::Post([cb, err] { cb(false, {}, err); });
            return;
        }
        auto d = J::Parse(r.body);
        User u = ParseUser(J::Root(d));
        Async::Post([cb, u] { cb(true, u, ""); });
    });
}

static void FetchRepoList(const std::string& url, ReposFn cb) {
    Async::Run(Async::Pool::Api, [url, cb = std::move(cb)] {
        Http::Response r = Call("GET", url);
        if (!r.Ok()) {
            std::string err = ErrorMessage(r);
            Async::Post([cb, err] { cb(false, {}, err); });
            return;
        }
        std::vector<Repo> out;
        auto d = J::Parse(r.body);
        yyjson_val* root = J::Root(d);
        yyjson_val* arr = root;
        // /search/repositories wraps the array in {items: ...}.
        if (yyjson_is_obj(root)) arr = J::Get(root, "items");
        for (yyjson_val* v : J::Arr(arr)) out.push_back(ParseRepo(v));
        Async::Post([cb, out] { cb(true, out, ""); });
    });
}

void FetchMyRepos(ReposFn cb) {
    FetchRepoList("https://api.github.com/user/repos?per_page=50&sort=pushed&affiliation=owner,collaborator",
                  std::move(cb));
}
void FetchStarred(ReposFn cb) {
    FetchRepoList("https://api.github.com/user/starred?per_page=50", std::move(cb));
}
void SearchRepos(const std::string& q, ReposFn cb) {
    FetchRepoList("https://api.github.com/search/repositories?per_page=30&q=" + Str::UrlEncode(q), std::move(cb));
}

void FetchRepo(const std::string& owner, const std::string& name, RepoFn cb) {
    const std::string url = "https://api.github.com/repos/" + owner + "/" + name;
    Async::Run(Async::Pool::Api, [url, cb = std::move(cb)] {
        Http::Response r = Call("GET", url);
        if (!r.Ok()) {
            std::string err = ErrorMessage(r);
            Async::Post([cb, err] { cb(false, {}, err); });
            return;
        }
        auto d = J::Parse(r.body);
        Repo rp = ParseRepo(J::Root(d));
        Async::Post([cb, rp] { cb(true, rp, ""); });
    });
}

void FetchBranches(const std::string& owner, const std::string& name, BranchesFn cb) {
    const std::string url = "https://api.github.com/repos/" + owner + "/" + name + "/branches?per_page=50";
    Async::Run(Async::Pool::Api, [url, cb = std::move(cb)] {
        Http::Response r = Call("GET", url);
        if (!r.Ok()) {
            std::string err = ErrorMessage(r);
            Async::Post([cb, err] { cb(false, {}, err); });
            return;
        }
        auto d = J::Parse(r.body);
        std::vector<Branch> out;
        for (yyjson_val* v : J::Arr(J::Root(d))) out.push_back(ParseBranch(v));
        Async::Post([cb, out] { cb(true, out, ""); });
    });
}

void FetchContents(const std::string& owner, const std::string& name, const std::string& ref, const std::string& path,
                   ContentsFn cb) {
    std::string p = path;
    // The API accepts an empty path for the repo root; otherwise percent-encode
    // each segment but keep "/" as a separator.
    std::string encoded;
    for (size_t i = 0; i < p.size();) {
        size_t slash = p.find('/', i);
        std::string seg = p.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!encoded.empty()) encoded += "/";
        encoded += Str::UrlEncode(seg);
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    std::string url = "https://api.github.com/repos/" + owner + "/" + name + "/contents";
    if (!encoded.empty()) url += "/" + encoded;
    if (!ref.empty()) url += "?ref=" + Str::UrlEncode(ref);
    Async::Run(Async::Pool::Api, [url, cb = std::move(cb)] {
        Http::Response r = Call("GET", url);
        if (!r.Ok()) {
            std::string err = ErrorMessage(r);
            Async::Post([cb, err] { cb(false, {}, err); });
            return;
        }
        auto d = J::Parse(r.body);
        yyjson_val* root = J::Root(d);
        std::vector<TreeEntry> out;
        if (yyjson_is_arr(root)) {
            for (yyjson_val* v : J::Arr(root)) out.push_back(ParseEntry(v));
        } else {
            // One object: it was a file path. Mark it as a single-file listing.
            out.push_back(ParseEntry(root));
        }
        Async::Post([cb, out] { cb(true, out, ""); });
    });
}

void FetchReleases(const std::string& owner, const std::string& name, ReleasesFn cb) {
    const std::string url = "https://api.github.com/repos/" + owner + "/" + name + "/releases?per_page=30";
    Async::Run(Async::Pool::Api, [url, cb = std::move(cb)] {
        Http::Response r = Call("GET", url);
        if (!r.Ok()) {
            std::string err = ErrorMessage(r);
            Async::Post([cb, err] { cb(false, {}, err); });
            return;
        }
        auto d = J::Parse(r.body);
        std::vector<Release> out;
        for (yyjson_val* v : J::Arr(J::Root(d))) out.push_back(ParseRelease(v));
        Async::Post([cb, out] { cb(true, out, ""); });
    });
}

void FetchText(const std::string& url, TextFn cb) {
    Async::Run(Async::Pool::Api, [url, cb = std::move(cb)] {
        Http::Request req;
        req.url = url;
        req.timeoutMs = 15000;
        req.maxBytes = 1u << 20;
        req.headers = {"User-Agent: SimpleNroFinder (ad3333m)"};
        Http::Response r = Http::Perform(req);
        if (!r.Ok()) {
            std::string err = r.error.empty() ? ("HTTP " + std::to_string(r.status)) : r.error;
            Async::Post([cb, err] { cb(false, "", err); });
            return;
        }
        std::string body = r.body;
        Async::Post([cb, body] { cb(true, body, ""); });
    });
}

}  // namespace Api
