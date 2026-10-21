#pragma once

#include <functional>
#include <string>
#include <vector>

#include "github/model.h"

// All GitHub REST calls go through here. Each entry point dispatches to the
// background pool and hands results back on the main thread via Async::Post.
namespace Api {

void SetToken(const std::string& token);      // "" = anonymous
const std::string& Token();
bool HaveToken();

// Verify a token by calling GET /user. ok=false carries the error.
using MeFn = std::function<void(bool ok, User me, std::string err)>;
void FetchMe(const std::string& token, MeFn cb);

// Repos the authed user owns or collaborates on, sorted by last pushed.
using ReposFn = std::function<void(bool ok, std::vector<Repo> list, std::string err)>;
void FetchMyRepos(ReposFn cb);
void FetchStarred(ReposFn cb);

// Public search for repositories, GitHub-style query.
void SearchRepos(const std::string& q, ReposFn cb);

// A single repo (owner/name) refreshed with up-to-date defaultBranch etc.
using RepoFn = std::function<void(bool ok, Repo repo, std::string err)>;
void FetchRepo(const std::string& owner, const std::string& name, RepoFn cb);

// Branches of a repo.
using BranchesFn = std::function<void(bool ok, std::vector<Branch> list, std::string err)>;
void FetchBranches(const std::string& owner, const std::string& name, BranchesFn cb);

// Directory listing (empty path = repo root). ref may be a branch, tag or sha.
using ContentsFn = std::function<void(bool ok, std::vector<TreeEntry> list, std::string err)>;
void FetchContents(const std::string& owner, const std::string& name, const std::string& ref, const std::string& path,
                   ContentsFn cb);

// Releases, newest first.
using ReleasesFn = std::function<void(bool ok, std::vector<Release> list, std::string err)>;
void FetchReleases(const std::string& owner, const std::string& name, ReleasesFn cb);

// Fetch a small text file for preview (returns body as text, capped at ~1 MB).
using TextFn = std::function<void(bool ok, std::string text, std::string err)>;
void FetchText(const std::string& url, TextFn cb);

}  // namespace Api
