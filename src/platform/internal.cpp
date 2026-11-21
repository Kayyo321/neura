#include "internal.hpp"

#ifdef _WIN32

namespace neura::platform::detail {

std::wstring api_path(const std::filesystem::path& path) {
    std::wstring text = path.native();
    for (auto& c : text) {
        if (c == L'/') c = L'\\';
    }
    if (text.starts_with(L"\\\\?\\") || text.starts_with(L"\\\\.\\")) return text;
    if (!path.is_absolute()) return text;
    // The prefix turns off "." and ".." processing, so normalize first.
    std::wstring normal = path.lexically_normal().make_preferred().native();
    if (normal.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + normal.substr(2);
    return L"\\\\?\\" + normal;
}

std::string current_user_sid() {
    HANDLE raw = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw)) return {};
    Handle token(raw);
    DWORD size = 0;
    ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (size == 0) return {};
    std::string buffer(size, '\0');
    if (!::GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size)) return {};
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    const DWORD length = ::GetLengthSid(user->User.Sid);
    std::string sid(length, '\0');
    if (!::CopySid(length, sid.data(), user->User.Sid)) return {};
    return sid;
}

} // namespace neura::platform::detail

#endif
