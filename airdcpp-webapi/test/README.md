# Version selection tests

The `npm_version_selection` CTest test calls the same selector used by
`NpmRepository`. It requires C++20 and nlohmann/json, but does not access the
registry, install extensions, or start the daemon.

It is included in the normal build unless `BUILD_TESTING=OFF`:

```sh
cmake --build build --target npm_version_test
ctest --test-dir build --output-on-failure -R npm_version_selection
```

To run only these tests without the daemon's other dependencies:

```sh
cmake -S airdcpp-webapi/test -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

If nlohmann/json is not in a system include directory, pass
`-DNLOHMANN_JSON_INCLUDE_DIR=/path/to/include` (the directory containing
`nlohmann/json.hpp`). Use the same JSON dependency as the application.

The tests cover numeric minor/patch/prerelease transitions, fresh installation,
stable/prerelease filters, same-major upgrades, the highest major notification,
equal precedence, and strict semver 1.0.1 parsing. Build metadata does not trigger
reinstallation; arbitrary prerelease labels obey the same installation filters.
The migration tests also cover case-sensitive identifiers, wider numeric
components, leading-zero rejection, and the parser's input limit. Each selection case
is serialized in both key orders before parsing. Invalid version keys propagate
errors, including keys that the old early return did not visit.

These are selector tests; they do not exercise HTTP callbacks, translated log
messages, downloading, or installation.
