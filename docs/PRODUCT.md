# Product definition

## Problem

NAM users can obtain thousands of captures, but the normal workflow is fragmented: browse a website, download files, build folders manually, open a file dialog in a plugin, play the same riff again, and repeat. Existing all-in-one products tend to add accounts, cloud coupling, large interfaces, or closed preset formats.

## Product promise

BRKNAM turns a folder of NAM captures into a searchable, auditionable instrument inside a DAW while remaining fully useful offline.

## Core user journey

1. Select one or more local library folders.
2. BRKNAM indexes `.nam` files and supported IR formats without moving them.
3. Search and filter by filename, metadata, creator, architecture, tags, favorites, and user rating.
4. Record a short clean-input audition loop once.
5. Step through results using keyboard, mouse, or MIDI and hear the same loop through every model.
6. Save a portable preset referencing local assets by path and content hash.
7. Optionally connect an approved online provider such as TONE3000 to select and download models.

## MVP scope

The first useful release contains:

- one NAM processing slot;
- one IR slot;
- local library indexing and search;
- previous/next model navigation;
- favorites and recent models;
- audition loop with A/B comparison;
- VST3, AU, and standalone builds for Windows and macOS;
- open JSON preset format;
- no mandatory account or telemetry.

## Explicit non-goals for MVP

- modeling or training amplifiers;
- a large collection of bundled captures;
- a full guitar multi-effects workstation;
- social features, recommendations, or proprietary cloud storage;
- AAX distribution;
- scraping or mirroring third-party catalogs.

## Success criteria

- A new user can point BRKNAM at an existing library and hear the first model within two minutes.
- Switching between prepared models causes no transport stop and no audible click.
- Search remains interactive with at least 25,000 indexed assets.
- The plugin works offline after installation.
- A session can restore a model even after its containing folder is moved, provided the file remains in an indexed library.
