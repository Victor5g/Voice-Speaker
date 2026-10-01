# Xcode temporary/cache exception

On 2026-09-11 the user explicitly approved the requested exception for internal
Xcode temporary and cache files outside the repository: “Autorizo sim!!”.

`project-local.sh` resolves the current user's `DARWIN_USER_TEMP_DIR` and
`DARWIN_USER_CACHE_DIR` with `getconf` and `pwd -P`, verifies that both are the
expected per-user directories under `/private/var/folders`, and permits toolchain
scratch writes there. It does not permit arbitrary writes to `/private/var`,
user configuration, global installation directories or services.

DerivedData, project-controlled caches, dependencies, models, tools and artifacts
remain in the repository. No new authorization is needed to reuse this exception.
