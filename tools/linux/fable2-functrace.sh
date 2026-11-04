#!/usr/bin/env bash
# Fable 2 - guest function tracing launcher (Linux port of fable2-functrace.cmd).
#
# Every guest function entry is logged by name to fable2_func_trace.log next
# to the binary; call counts go to fable2_func_summary.log (refreshed every 5 s).
#
# Keywords, in any order:
#   ./fable2-functrace.sh                    trace every call (log + summary)
#   ./fable2-functrace.sh subs               only sub_* functions (naming mode)
#   ./fable2-functrace.sh subs LoadingScreen naming mode + substring filter
#   ./fable2-functrace.sh summary            summary file only
#   ./fable2-functrace.sh trace              trace log only
# Any other word is used as FABLE2_FUNC_TRACE_FILTER (substring match).
#
# WARNING: unfiltered tracing writes several GB per minute of gameplay.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

filter=""; subs=0; want_log=0; want_summary=0
for a in "$@"; do
  case "${a,,}" in
    subs) subs=1 ;;
    trace) want_log=1 ;;
    summary) want_summary=1 ;;
    vulkan|d3d12|prebuilt) ;;  # Linux always renders with Vulkan
    *) [[ -z "$filter" ]] && filter="$a" ;;
  esac
done

export FABLE2_FUNC_TRACE=1 FABLE2_FUNC_TRACE_LOG=1 FABLE2_FUNC_TRACE_SUMMARY=1
mode="trace + summary"
if (( want_log && !want_summary )); then export FABLE2_FUNC_TRACE_SUMMARY=0; mode="trace only"; fi
if (( want_summary && !want_log )); then export FABLE2_FUNC_TRACE_LOG=0; mode="summary only"; fi
[[ -n "$filter" ]] && export FABLE2_FUNC_TRACE_FILTER="$filter"
(( subs )) && export FABLE2_FUNC_TRACE_SUBS_ONLY=1

for f in fable2_func_trace fable2_func_summary; do
  [[ -f "$f.log" ]] && mv -f "$f.log" "${f}_prev.log"
done

echo "fable2-functrace: subs_only=$subs filter='${filter}' ($mode)" >&2
exec ./fable_2
