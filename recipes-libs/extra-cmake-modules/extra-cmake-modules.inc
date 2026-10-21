# SPDX-FileCopyrightText: 2017-2020 Volker Krause <vkrause@kde.org>
# SPDX-FileCopyrightText: 2020 Andreas Cord-Landwehr <cordlandwehr@kde.org>
#
# SPDX-License-Identifier: MIT

LICENSE = "BSD-2-Clause & BSD-3-Clause & MIT"
LIC_FILES_CHKSUM = "file://LICENSES/MIT.txt;md5=38aa75cf4c4c87f018227d5ec9638d75"
PR = "r0"

DEPENDS = "cmake-native qttools qtbase-native qttools-native"

require extra-cmake-modules_metainfo.inc
inherit cmake_qt5


CLEANBROKEN = "1"

RDEPENDS:${PN}-dev = ""

FILES:${PN}-dev += " ${datadir}/ECM"

BBCLASSEXTEND = "native nativesdk"
