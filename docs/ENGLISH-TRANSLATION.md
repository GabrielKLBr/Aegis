# English repository text

Documentation, source comments, application labels, command-line messages, generated package readmes, and progress reports use English. Portuguese document filenames have been renamed and references updated. API names, protocol fields, persisted identifiers, command-line options, and existing archive contents retain their contracts. Newly generated package readmes use README names. Unicode filenames used as regression inputs remain unchanged to preserve non-ASCII coverage.

## Validation scope

This is a language update, not a new MiniNT compatibility phase. Windows Release builds passed for `avui`, `av`, `guardian-engine-service`, `guardian-gui`, `guardian-gui-pe`, and `guardian_native_controls_widgets`, including their build dependencies. Publication-tool tests passed (21 cases, with one platform-specific symlink test skipped on Windows); all seven progress-site tests passed. Python parsing, JSON parsing, source-structure comparison, documentation links, and section anchors were checked. The generated site contains English text and uses `lang="en"`.

Full historical phase suites, GUI interaction campaigns, Linux builds, installed-service updates, and new private build archives were not run as part of this text-only update. Historical phase test counts and archived evidence refer to their original revisions; translation does not constitute a new acceptance campaign for those phases.
