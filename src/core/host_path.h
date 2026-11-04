// fable_2 - Case-insensitive host path lookup
//
// Xbox 360 (and Windows) file names are case-insensitive; Linux file systems
// are not. The SDK's virtual file system already falls back to a
// case-insensitive lookup for everything the game opens, but a few host-side
// checks (content markers in xex_verify.h, dir.manifest in
// dir_manifest_heal.h) open disc files directly. ResolvePath() makes those
// checks find e.g. "data/Language/de-de" when the catalogue says
// "data/language/de-de".

#pragma once

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace fable2::hostpath {

inline bool IEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

// Returns root / rel with each component of `rel` matched case-insensitively
// against what exists on disk. Components that do not exist are appended as
// given, so the result is always usable (and simply won't exist). On Windows
// the file system is already case-insensitive, so this is plain root / rel.
inline std::filesystem::path ResolvePath(const std::filesystem::path& root,
                                         const std::filesystem::path& rel) {
#ifdef _WIN32
  return root / rel;
#else
  std::filesystem::path cur = root;
  bool exact_so_far = true;
  for (const auto& part : rel) {
    const std::string name = part.string();
    if (name.empty() || name == "/") continue;
    std::error_code ec;
    if (exact_so_far && std::filesystem::exists(cur / part, ec)) {
      cur /= part;
      continue;
    }
    bool matched = false;
    if (exact_so_far) {
      for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
        if (IEquals(entry.path().filename().string(), name)) {
          cur = entry.path();
          matched = true;
          break;
        }
      }
    }
    if (!matched) {
      cur /= part;
      exact_so_far = false;  // nothing below a missing component can exist
    }
  }
  return cur;
#endif
}

}  // namespace fable2::hostpath
