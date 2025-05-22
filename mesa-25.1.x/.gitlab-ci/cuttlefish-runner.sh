#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

. "${SCRIPTS_DIR}/setup-test-env.sh"

section_start cuttlefish_setup "cuttlefish: setup"
set -xe

# Structured tagging check for angle
if [ -n "$ANGLE_TAG" ]; then
    # Bail out if the ANGLE_TAG differs from what is offered in the system
    ci_tag_test_time_check "ANGLE_TAG"
fi

export PATH=/cuttlefish/bin:$PATH
export LD_LIBRARY_PATH=/cuttlefish/lib64:${CI_PROJECT_DIR}/install/lib:$LD_LIBRARY_PATH

# Pick up a vulkan driver
ARCH=$(uname -m)
export VK_DRIVER_FILES=${CI_PROJECT_DIR}/install/share/vulkan/icd.d/${VK_DRIVER:-}_icd.$ARCH.json

syslogd

chown root:kvm /dev/kvm

pushd /cuttlefish

# Add a function to perform some tasks when exiting the script
function my_atexit()
{
  # shellcheck disable=SC2317
  HOME=/cuttlefish stop_cvd -wait_for_launcher=40

  # shellcheck disable=SC2317
  cp /cuttlefish/cuttlefish/instances/cvd-1/logs/logcat $RESULTS_DIR || true
  # shellcheck disable=SC2317
  cp /cuttlefish/cuttlefish/instances/cvd-1/kernel.log $RESULTS_DIR || true
  # shellcheck disable=SC2317
  cp /cuttlefish/cuttlefish/instances/cvd-1/logs/launcher.log $RESULTS_DIR || true
}

# stop cuttlefish if the script ends prematurely or is interrupted
trap 'my_atexit' EXIT
trap 'exit 2' HUP INT PIPE TERM

ulimit -S -n 32768

VSOCK_BASE=10000 # greater than all the default vsock ports
VSOCK_CID=$((VSOCK_BASE + (CI_JOB_ID & 0xfff)))

HOME=/cuttlefish launch_cvd \
  -daemon \
  -verbosity=VERBOSE \
  -file_verbosity=VERBOSE \
  -use_overlay=false \
  -vsock_guest_cid=$VSOCK_CID \
  -enable_audio=false \
  -enable_bootanimation=false \
  -enable_minimal_mode=true \
  -enable_modem_simulator=false \
  -guest_enforce_security=false \
  -report_anonymous_usage_stats=no \
  -gpu_mode="$ANDROID_GPU_MODE" \
  -cpus=${FDO_CI_CONCURRENT:-4} \
  -memory_mb 8192 \
  -kernel_path="/cuttlefish/bzImage" \
  -initramfs_path="/cuttlefish/initramfs.img"

sleep 1

popd

# shellcheck disable=SC2034 # used externally
ADB=adb

# The script exits with the appropriate exit code
. "$(dirname "$0")/android-runner.sh"
