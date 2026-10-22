# Vendored semver

`semver.hpp` is an unmodified copy of Neargye/semver **v1.0.1**:

- Source: https://github.com/Neargye/semver/blob/v1.0.1/include/semver.hpp
- Git blob: `5182ea493680402efd7f71a97c538d9470ad2be3`
- License: MIT (included in the header)
- Reference: https://github.com/Neargye/semver/blob/v1.0.1/doc/reference.md

The npm extension selector uses the runtime API with strict parsing, normal
SemVer precedence (ignoring build metadata), and `is_prerelease()`. It does not
require compile-time parsing or the version literal. The application remains
C++20; the library's runtime API also supports C++17.

Compared with the previous 0.3.0 header, 1.0.1 supports arbitrary valid prerelease
identifiers and build metadata. Leading-zero core/numeric prerelease components
are rejected, and prerelease identifiers are case sensitive. The default core
component type is uint32_t and the default maximum input length is 512 bytes.
These are strict SemVer rules and library limits, not a claim of compatibility
with every node-semver coercion or range option.
