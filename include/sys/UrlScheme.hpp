#pragma once

#include <QString>

// Portable install: the exe path moves, so registration re-runs at startup and is diffed against the settings mirror (url_scheme_mirror).

// Opaque per-platform state, revision-prefixed so that adding entries re-registers installs that never moved; empty when unsupported.
QString UrlScheme_DesiredState();

void UrlScheme_Apply();

void UrlScheme_RegisterIfNeeded();
