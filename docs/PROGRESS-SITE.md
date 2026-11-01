# Public progress of the MiniNT

GitHub Pages publishes three pages from `main`:

- `index.html`: entry with links to both reports;
- `progress.html`: existing video, interface and audio inventories;
- `windows-apis.html`: General catalogue by DLL, including roadmap objectives.

The `badges/geral.svg` and `badges/interface.svg` badges are generated at the site and connected at the start of the README. The percentage sums entries implemented and with restrictions, divided by all entries in the respective catalog. Declarations without validated contract, pending and refusals do not enter the numerator. The measure does not represent compatibility with all Windows.

Manage locally with Python, without external dependencies:

```text
python -B tools/windows_api_progress.py --output build-progress
```

The generator also executes `tools/progress.py`, preserving `progress.html` and `progress.json`. The general catalogue uses `.def`, adapter statements, literal inventories of export generators, and versioned matrices of graphic and audio contracts. It includes the management cutout of 14.11. It does not matter or execute runtime generators to extract their inventories.

An export declaration receives **Present in the code — validate contract**. It does not automatically become an implemented or validated API. Explicit matrices have priority over gross declarations. Variants of previous phases are gathered by the DLL/symbol identity, with links to the sources; this does not guarantee simultaneous availability in a single package/profile NT.

The initial goals in `PLAN` in the generator are an extensible list of APIs required for the planned scenarios. The site is not an exhaustive inventory of Windows SDK, it does not list all the COM/WinRT methods and does not claim that finishing this list will make all Windows programs work. Signatures, semantics, dependencies and differences per NT version need own contracts and tests.

The `.github/workflows/progress-pages.yml` stream regenerates and publishes the site when the relevant inventories or sources change. Only generated HTML and JSON are published, along with SVG badges; Microsoft binaries and references are not part of the site.
