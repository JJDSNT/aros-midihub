#!/usr/bin/env bash
# Configure the pinned AROS contrib Git submodule as a sparse Radium checkout.
set -euo pipefail
usage() { echo "Usage: $0 [--aros-tree /path/to/AROS]"; }
AROS_TREE=""
while (($#)); do
  case "$1" in
    --aros-tree) (($# >= 2)) || { usage >&2; exit 2; }; AROS_TREE="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
SUBMODULE="$ROOT/third_party/aros-contrib"
# Initialize without checkout, so sparse settings precede materialization.
git -C "$ROOT" submodule update --init --no-checkout -- "$SUBMODULE"
git -C "$SUBMODULE" sparse-checkout init --cone
git -C "$SUBMODULE" sparse-checkout set MultiMedia/radium
git -C "$ROOT" submodule update --checkout -- "$SUBMODULE"
RADIUM="$SUBMODULE/MultiMedia/radium"
[[ -f "$RADIUM/mmakefile.src" ]] || { echo "Radium MetaMake source missing: $RADIUM" >&2; exit 1; }
echo "Radium available: $RADIUM"
if [[ -n "$AROS_TREE" ]]; then
  [[ -d "$AROS_TREE/contrib" ]] || { echo "Missing AROS contrib/: $AROS_TREE" >&2; exit 1; }
  AROS_TREE="$(cd "$AROS_TREE" && pwd -P)"
  link_safely() {
    local target="$1" link="$2"
    mkdir -p "$(dirname "$link")"
    if [[ -L "$link" ]] && [[ "$(readlink -f "$link")" == "$(readlink -f "$target")" ]]; then
      echo "Already linked: $link"; return
    fi
    if [[ -e "$link" || -L "$link" ]]; then
      echo "Refusing to replace existing AROS path: $link" >&2; exit 1
    fi
    ln -s "$target" "$link"
    echo "Linked $link -> $target"
  }
  link_safely "$ROOT" "$AROS_TREE/contrib/aros-midihub"
  link_safely "$RADIUM" "$AROS_TREE/contrib/MultiMedia/radium"
fi
echo "Radium is optional and is not included in the MIDIHub default build."
