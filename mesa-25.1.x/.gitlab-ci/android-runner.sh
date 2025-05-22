#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

# Set default ADB command if not set already

: "${ADB:=adb}"

$ADB wait-for-device root
sleep 1

# overlay 

REMOUNT_PATHS="/vendor"
if [ "$ANDROID_VERSION" -ge 15 ]; then
  REMOUNT_PATHS="$REMOUNT_PATHS /system"
fi

OV_TMPFS="/data/overlay-remount"
$ADB shell mkdir -p "$OV_TMPFS"
$ADB shell mount -t tmpfs none "$OV_TMPFS"

for path in $REMOUNT_PATHS; do
  $ADB shell mkdir -p "${OV_TMPFS}${path}-upper"
  $ADB shell mkdir -p "${OV_TMPFS}${path}-work"

  opts="lowerdir=${path},upperdir=${OV_TMPFS}${path}-upper,workdir=${OV_TMPFS}${path}-work"
  $ADB shell mount -t overlay -o "$opts" none ${path}
done

$ADB shell setenforce 0

# download Android Mesa from S3
MESA_ANDROID_ARTIFACT_URL=https://${PIPELINE_ARTIFACTS_BASE}/${S3_ANDROID_ARTIFACT_NAME}.tar.zst
curl -L --retry 4 -f --retry-all-errors --retry-delay 60 -o ${S3_ANDROID_ARTIFACT_NAME}.tar.zst ${MESA_ANDROID_ARTIFACT_URL}
mkdir /mesa-android
tar -C /mesa-android -xvf ${S3_ANDROID_ARTIFACT_NAME}.tar.zst
rm "${S3_ANDROID_ARTIFACT_NAME}.tar.zst" &

INSTALL="/mesa-android/install"

# replace libraries

$ADB shell rm -f /vendor/lib64/libgallium_dri.so*
$ADB shell rm -f /vendor/lib64/egl/libEGL_mesa.so*
$ADB shell rm -f /vendor/lib64/egl/libGLESv1_CM_mesa.so*
$ADB shell rm -f /vendor/lib64/egl/libGLESv2_mesa.so*

$ADB push "$INSTALL/lib/libgallium_dri.so" /vendor/lib64/libgallium_dri.so
$ADB push "$INSTALL/lib/libEGL.so" /vendor/lib64/egl/libEGL_mesa.so
$ADB push "$INSTALL/lib/libGLESv1_CM.so" /vendor/lib64/egl/libGLESv1_CM_mesa.so
$ADB push "$INSTALL/lib/libGLESv2.so" /vendor/lib64/egl/libGLESv2_mesa.so

$ADB shell rm -f /vendor/lib64/hw/vulkan.lvp.so*
$ADB shell rm -f /vendor/lib64/hw/vulkan.virtio.so*
$ADB shell rm -f /vendor/lib64/hw/vulkan.intel.so*

$ADB push "$INSTALL/lib/libvulkan_lvp.so" /vendor/lib64/hw/vulkan.lvp.so
$ADB push "$INSTALL/lib/libvulkan_virtio.so" /vendor/lib64/hw/vulkan.virtio.so
$ADB push "$INSTALL/lib/libvulkan_intel.so" /vendor/lib64/hw/vulkan.intel.so

$ADB shell rm -f /vendor/lib64/egl/libEGL_emulation.so*
$ADB shell rm -f /vendor/lib64/egl/libGLESv1_CM_emulation.so*
$ADB shell rm -f /vendor/lib64/egl/libGLESv2_emulation.so*

ANGLE_DEST_PATH=/vendor/lib64/egl
if [ "$ANDROID_VERSION" -ge 15 ]; then
  ANGLE_DEST_PATH=/system/lib64
fi

$ADB shell rm -f "$ANGLE_DEST_PATH/libEGL_angle.so"*
$ADB shell rm -f "$ANGLE_DEST_PATH/libGLESv1_CM_angle.so"*
$ADB shell rm -f "$ANGLE_DEST_PATH/libGLESv2_angle.so"*

$ADB push /angle/libEGL_angle.so "$ANGLE_DEST_PATH/libEGL_angle.so"
$ADB push /angle/libGLESv1_CM_angle.so "$ANGLE_DEST_PATH/libGLESv1_CM_angle.so"
$ADB push /angle/libGLESv2_angle.so "$ANGLE_DEST_PATH/libGLESv2_angle.so"

get_gles_runtime_version() {
  while [ "$($ADB shell dumpsys SurfaceFlinger | grep GLES:)" = "" ] ; do sleep 1; done
  $ADB shell dumpsys SurfaceFlinger | grep GLES
}

# Check what GLES implementation is used before loading the new libraries
get_gles_runtime_version

# restart Android shell, so that services use the new libraries
$ADB shell stop
$ADB shell start

# Check what GLES implementation is used after loading the new libraries
GLES_RUNTIME_VERSION="$(get_gles_runtime_version)"

if [ -n "$ANGLE_TAG" ]; then
  # Note: we are injecting the ANGLE libs too, so we need to check if the
  #       ANGLE libs are being used after the shell restart.
  ANGLE_HASH=$(head -c 12 /angle/version)
  if ! printf "%s" "$GLES_RUNTIME_VERSION" | grep --quiet "${ANGLE_HASH}"; then
    echo "Fatal: Android is loading a wrong version of the ANGLE libs: ${ANGLE_HASH}" 1>&2
    exit 1
  fi
else
  MESA_BUILD_VERSION=$(cat "$INSTALL/VERSION")
  if ! printf "%s" "$GLES_RUNTIME_VERSION" | grep --quiet "${MESA_BUILD_VERSION}$"; then
     echo "Fatal: Android is loading a wrong version of the Mesa3D GLES libs: ${GLES_RUNTIME_VERSION}" 1>&2
     exit 1
  fi
fi

if [ -n "$USE_ANDROID_CTS" ]; then
  # The script sets EXIT_CODE
  . "$(dirname "$0")/android-cts-runner.sh"
else
  # The script sets EXIT_CODE
  . "$(dirname "$0")/android-deqp-runner.sh"
fi

exit $EXIT_CODE
