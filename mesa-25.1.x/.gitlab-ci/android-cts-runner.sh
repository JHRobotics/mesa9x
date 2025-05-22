#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

. "${SCRIPTS_DIR}/setup-test-env.sh"

export PATH=/android-tools/android-cts/jdk/bin/:/android-tools/build-tools:$PATH
export JAVA_HOME=/android-tools/android-cts/jdk

# Wait for the appops service to show up
while [ "$($ADB shell dumpsys -l | grep appops)" = "" ] ; do sleep 1; done

SKIP_FILE="$INSTALL/${GPU_VERSION}-android-cts-skips.txt"

EXCLUDE_FILTERS=""
if [ -e "$SKIP_FILE" ]; then
  EXCLUDE_FILTERS="$(grep -v -E "(^#|^[[:space:]]*$)" "$SKIP_FILE" | sed -s 's/.*/--exclude-filter "\0" /g')"
fi

INCLUDE_FILE="$INSTALL/${GPU_VERSION}-android-cts-include.txt"

if [ -e "$INCLUDE_FILE" ]; then
  INCLUDE_FILTERS="$(grep -v -E "(^#|^[[:space:]]*$)" "$INCLUDE_FILE" | sed -s 's/.*/--include-filter "\0" /g')"
else
  INCLUDE_FILTERS=$(printf -- "--include-filter %s " $ANDROID_CTS_MODULES | sed -e 's/ $//g')
fi

set +e
eval "/android-tools/android-cts/tools/cts-tradefed" run commandAndExit cts-dev \
  $EXCLUDE_FILTERS \
  $INCLUDE_FILTERS

[ "$(grep "^FAILED" /android-tools/android-cts/results/latest/invocation_summary.txt | tr -d ' ' | cut -d ':' -f 2)" = "0" ]

# shellcheck disable=SC2034 # EXIT_CODE is used by the script that sources this one
EXIT_CODE=$?
set -e

section_switch cuttlefish_results "cuttlefish: gathering the results"

cp -r "/android-tools/android-cts/results/latest"/* $RESULTS_DIR
cp -r "/android-tools/android-cts/logs/latest"/* $RESULTS_DIR

section_end cuttlefish_results
