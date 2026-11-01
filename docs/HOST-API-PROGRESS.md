# Host-mediated API Panel

Generate the page and data without installing dependencies:

    python -B tools/progress.py --output build-progress

Open build-progress/progress.html or index.html. Search and filters work locally without connection. HTML and progress.json files are generated; they do not need to be versioned.

The dashboard uses config/gdi-api-matrix.json and config/software-graphics-api-matrix.json and config/audio-api-matrix.json. Restricted functions and COM methods with local status are identified separately from pending and refusals. The percentage refers to the listed inventory, not to the full Windows SDK, nor to the percentage of compatible programs. Inherited methods may appear in more than one interface.

Audio has a PCM/WAV, WinMM and WASAPI clipping shared from 14.10G, with local status, native operations and limits indicated by input. Media Foundation and compressed media remain pending. The dashboard only informs the inventory; it does not enable the audio bridge or device capture.

The workflow .github/workflows/progress-pages.yml regenerates and publishes only the build-progress directory by GitHub Pages when changing inventories, generator or workflow itself in branch main. Builds, evidence of tests, DLLs and local files are not part of this published directory.
