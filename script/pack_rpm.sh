#!/bin/bash
set -e

# Wraps an already-deployed portable build into an RPM, the same way
# pack_debian.sh does for Debian. Arguments mirror it exactly:
#   pack_rpm.sh <version> <arch> [systemqt]
# where <arch> is the Throned name (amd64 / arm64), not the RPM one.

VERSION="$1"
ARCH="$2"
SYSTEM_QT="$3"

case "$ARCH" in
    amd64) RPM_ARCH="x86_64" ;;
    arm64) RPM_ARCH="aarch64" ;;
    *) echo "unknown architecture: $ARCH" >&2; exit 1 ;;
esac

SUFFIX=$([[ $SYSTEM_QT == "systemqt" ]] && echo "-system-qt")
SOURCE="linux-$ARCH$SUFFIX"
[[ -d "$SOURCE" ]] || { echo "missing deployment directory: $SOURCE" >&2; exit 1; }

BUILD_ROOT="$PWD/rpmbuild"
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# Stage the payload exactly as it will be installed: /opt/Throned plus a desktop
# entry and an icon the desktop database can find.
STAGE="$BUILD_ROOT/stage"
mkdir -p "$STAGE/opt" "$STAGE/usr/share/applications" "$STAGE/usr/share/pixmaps"
cp -r "$SOURCE" "$STAGE/opt/Throned"
rm -f "$STAGE/opt/Throned/Throned.debug"
cp "$STAGE/opt/Throned/Throned.png" "$STAGE/usr/share/pixmaps/Throned.png"

cat >"$STAGE/usr/share/applications/Throned.desktop" <<-EOF
[Desktop Entry]
Name=Throned
Comment=Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
Exec=sh -c "PATH=/opt/Throned:\$PATH /opt/Throned/Throned -appdata"
Icon=/opt/Throned/Throned.png
Terminal=false
Type=Application
Categories=Network;Application;
EOF

# Fedora and openSUSE name the Qt packages differently from Debian, so the
# system-Qt build depends on the Fedora names and lets rpm resolve the rest.
if [[ $SYSTEM_QT == "systemqt" ]]; then
    REQUIRES="Requires:       qt6-qtbase-gui, qt6-qtsvg, qt6-qtwayland, xcb-util-cursor, google-noto-emoji-color-fonts"
else
    REQUIRES=""
fi

cat >"$BUILD_ROOT/SPECS/throned.spec" <<-EOF
Name:           throned
Version:        $VERSION
Release:        1
Summary:        Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
License:        GPL-3.0-or-later
URL:            https://github.com/troshkindm/throned
BuildArch:      $RPM_ARCH
$REQUIRES
Requires:       desktop-file-utils
# The payload is a self-contained deployment; rpm's own dependency scan would
# otherwise demand every library the bundled Qt happens to link against.
AutoReqProv:    no

%description
Throned is a Qt desktop proxy client powered by sing-box and Xray. This package
installs a self-contained build under /opt/Throned.

%install
mkdir -p %{buildroot}
cp -a $STAGE/. %{buildroot}/

%post
update-desktop-database &>/dev/null || :

%postun
update-desktop-database &>/dev/null || :

%files
/opt/Throned
/usr/share/applications/Throned.desktop
/usr/share/pixmaps/Throned.png

%changelog
EOF

rpmbuild --define "_topdir $BUILD_ROOT" -bb "$BUILD_ROOT/SPECS/throned.spec"
mv "$BUILD_ROOT/RPMS/$RPM_ARCH"/*.rpm ./Throned.rpm
rm -rf "$BUILD_ROOT"
