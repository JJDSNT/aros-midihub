#!/usr/bin/env bash
# Prepare the historical AROS Radium port without vendoring contrib into MIDIHub.
set -euo pipefail

usage() {
  echo "Usage: $0 [--aros-tree /path/to/AROS] [--workspace /path/to/workspace]"
  echo "       $0 --help"
}
AROS_TREE=""
WORKSPACE=""
while (($#)); do
  case "$1" in
    --aros-tree|--workspace)
      (($# >= 2)) || { usage >&2; exit 2; }
      if [[ "$1" == "--aros-tree" ]]; then AROS_TREE="$2"; else WORKSPACE="$2"; fi
      shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
command -v git >/dev/null || { echo "git is required" >&2; exit 1; }
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
if [[ -z "$WORKSPACE" ]]; then WORKSPACE="$(dirname "$ROOT")"; fi
mkdir -p "$WORKSPACE"
WORKSPACE="$(cd "$WORKSPACE" && pwd -P)"
CHECKOUT="$WORKSPACE/aros-contrib-radium"
CONTRIB_URL="https://github.com/aros-development-team/contrib.git"
CONTRIB_COMMIT="a13eda718af94f383c6ec35bfc7d5a88704ba775"

if [[ ! -e "$CHECKOUT" ]]; then
  git clone --filter=blob:none --no-checkout --sparse "$CONTRIB_URL" "$CHECKOUT"
  git -C "$CHECKOUT" sparse-checkout set MultiMedia/radium
elif [[ ! -d "$CHECKOUT/.git" ]]; then
  echo "Refusing to use non-Git directory: $CHECKOUT" >&2
  exit 1
fi
ACTUAL_URL="$(git -C "$CHECKOUT" remote get-url origin)"
if [[ "$ACTUAL_URL" != "$CONTRIB_URL" ]]; then
  echo "Unexpected origin in $CHECKOUT: $ACTUAL_URL" >&2
  exit 1
fi
if [[ -n "$(git -C "$CHECKOUT" status --porcelain)" ]]; then
  echo "Radium checkout has local changes; refusing to overwrite them." >&2
  exit 1
fi
git -C "$CHECKOUT" fetch --depth=1 origin "$CONTRIB_COMMIT"
git -C "$CHECKOUT" sparse-checkout set MultiMedia/radium
git -C "$CHECKOUT" checkout --detach "$CONTRIB_COMMIT"
RADIUM="$CHECKOUT/MultiMedia/radium"
[[ -f "$RADIUM/mmakefile.src" ]] || { echo "Radium MetaMake file missing" >&2; exit 1; }
echo "Radium ready at: $RADIUM"
echo "Pinned contrib commit: $CONTRIB_COMMIT"

if [[ -n "$AROS_TREE" ]]; then
  [[ -d "$AROS_TREE/contrib" ]] || { echo "Not an AROS source tree (missing contrib/): $AROS_TREE" >&2; exit 1; }
  AROS_TREE="$(cd "$AROS_TREE" && pwd -P)"
  link_safely() {
    local target="$1" link="$2"
    mkdir -p "$(dirname "$link")"
    if [[ -L "$link" ]]; then
      if [[ "$(readlink -f "$link")" == "$(readlink -f "$target")" ]]; then
        echo "Already linked: $link"
        return
      fi
      echo "Refusing to replace existing symlink: $link" >&2
      exit 1
    fi
    if [[ -e "$link" ]]; then
      echo "Refusing to replace existing path: $link" >&2
      exit 1
    fi
    ln -s "$target" "$link"
    echo "Linked: $link -> $target"
  }
  # MIDIHub's existing mmakefile.src explicitly expects contrib/extras/aros-midihub.
  link_safely "$ROOT" "$AROS_TREE/contrib/extras/aros-midihub"
  # Radium retains its original contrib/MultiMedia/radium MetaMake location.
  link_safely "$RADIUM" "$AROS_TREE/contrib/MultiMedia/radium"
fi
echo "No build or packaging was performed. Radium is optional and not part of MIDIHub's default package."
