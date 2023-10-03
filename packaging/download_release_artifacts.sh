#!/bin/bash
#
#  SPDX-License-Identifier: GPL-3.0-or-later
#

# exit when any command fails
set -e

KRITA_VERSION=5.2.0
CHANNEL=Stable

if echo $KRITA_VERSION | grep beta > /dev/null; then
   CHANNEL=Beta
elif echo $KRITA_VERSION | grep rc > /dev/null; then
   # release candidates are also considered as "beta" for updater
   CHANNEL=Beta
fi


WIN64_ARTIFACTS_URL=https://binary-factory.kde.org/job/Krita_Release_Windows64_Build/lastSuccessfulBuild/artifact
APPIMAGE_ARTIFACTS_URL=https://binary-factory.kde.org/job/Krita_Release_Appimage_Build/lastSuccessfulBuild/artifact
OSX_ARTIFACTS_URL=https://binary-factory.kde.org/job/Krita_Release_MacOS_Build/lastSuccessfulBuild/artifact

ANDROID_ARM64_V8A_ARTIFACTS_URL=https://binary-factory.kde.org/job/Krita_Release_Android_arm64-v8a_Build/lastSuccessfulBuild/artifact
ANDROID_ARMEABI_V7A_ARTIFACTS_URL=https://binary-factory.kde.org/job/Krita_Release_Android_armeabi-v7a_Build/lastSuccessfulBuild/artifact
ANDROID_X86_64_ARTIFACTS_URL=https://binary-factory.kde.org/job/Krita_Release_Android_x86_64_Build/lastSuccessfulBuild/artifact
ANDROID_X86_ARTIFACTS_URL=https://binary-factory.kde.org/job/Krita_Release_Android_x86_Build/lastSuccessfulBuild/artifact
ANDROID_APPBUNDLE_ARTIFACTS_URL=https://binary-factory.kde.org/job/Krita_Release_Android_AppBundle_Build/lastSuccessfulBuild/artifact

wget $WIN64_ARTIFACTS_URL/krita-x64-$KRITA_VERSION-dbg.zip
wget $WIN64_ARTIFACTS_URL/krita-x64-$KRITA_VERSION.zip
wget $WIN64_ARTIFACTS_URL/krita-x64-$KRITA_VERSION-setup.exe

wget $APPIMAGE_ARTIFACTS_URL/krita-$KRITA_VERSION-x86_64.appimage
wget $APPIMAGE_ARTIFACTS_URL/Krita-CHANGEME-x86_64.appimage.zsync

mv Krita-CHANGEME-x86_64.appimage.zsync Krita-$CHANNEL-x86_64.appimage.zsync

wget $ANDROID_ARM64_V8A_ARTIFACTS_URL/krita-arm64-v8a-$KRITA_VERSION-release-unsigned.apk
wget $ANDROID_ARMEABI_V7A_ARTIFACTS_URL/krita-armeabi-v7a-$KRITA_VERSION-release-unsigned.apk
wget $ANDROID_X86_ARTIFACTS_URL/krita-x86-$KRITA_VERSION-release-unsigned.apk
wget $ANDROID_X86_64_ARTIFACTS_URL/krita-x86_64-$KRITA_VERSION-release-unsigned.apk

wget $ANDROID_APPBUNDLE_ARTIFACTS_URL/krita-$KRITA_VERSION-release.aab
mv krita-$KRITA_VERSION-release.aab krita-$KRITA_VERSION-release-unsigned.aab

if [[ -f archive.zip ]]; then
   rm archive.zip
fi

if [[ -d archive ]]; then
   rm -d archive
fi

wget $OSX_ARTIFACTS_URL/*zip*/archive.zip
unzip archive.zip
mv archive/krita-nightly_*.dmg krita-$KRITA_VERSION.dmg
rm archive.zip
rm -rf archive
