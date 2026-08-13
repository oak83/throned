#!/bin/bash
set -e

VERSION="$1"
ARCH="$2"

mkdir -p Throned/DEBIAN
mkdir -p Throned/opt
cp -r linux-$ARCH$([[ $3 == "systemqt" ]] && echo "-system-qt") Throned/opt
mv Throned/opt/linux-$ARCH$([[ $3 == "systemqt" ]] && echo "-system-qt") Throned/opt/Throned
rm Throned/opt/Throned/Throned.debug

# basic
cat >Throned/DEBIAN/control <<-EOF
Package: throned
Version: $VERSION
Architecture: $ARCH
Maintainer: Mahdi Mahdi.zrei@gmail.com
Depends: desktop-file-utils$([[ $3 == "systemqt" ]] && echo ", libqt6core6, libqt6gui6, libqt6network6, libqt6widgets6, qt6-qpa-plugins, qt6-wayland, qt6-gtk-platformtheme, qt6-xdgdesktopportal-platformtheme, libxcb-cursor0, fonts-noto-color-emoji")
Description: Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
EOF

cat >Throned/DEBIAN/postinst <<-EOF
cat >/usr/share/applications/Throned.desktop<<-END
[Desktop Entry]
Name=Throned
Comment=Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
Exec=sh -c "PATH=/opt/Throned:\$PATH /opt/Throned/Throned -appdata"
Icon=/opt/Throned/Throned.png
Terminal=false
Type=Application
Categories=Network;Application;
END

update-desktop-database
EOF

sudo chmod 0755 Throned/DEBIAN/postinst

# desktop && PATH

sudo dpkg-deb --build Throned
