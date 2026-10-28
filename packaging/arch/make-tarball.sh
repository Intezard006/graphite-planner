#!/usr/bin/env bash
# Builds dist/graphite-planner-<version>-arch.tar.gz : the file you give to other Arch users.
# Run it from anywhere inside the repository:  packaging/arch/make-tarball.sh
set -euo pipefail    # stop at the first error, on unset variables, and on failed pipes

# Find the repository root (the folder that holds CMakeLists.txt).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

# The version comes from the "project(graphite VERSION x.y.z ...)" line in CMakeLists.txt.
VERSION="$(sed -n 's/^project(graphite VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$VERSION" ] || { echo "Could not read the version from CMakeLists.txt" >&2; exit 1; }

NAME="graphite-planner"
SRC_NAME="$NAME-$VERSION"          # folder name inside the source tarball
REL_NAME="$NAME-$VERSION-arch"     # folder name inside the release tarball

# git archive packs what is committed (HEAD), not your working files, so refuse when they differ.
if [ -n "$(git status --porcelain)" ]; then
    echo "Uncommitted changes found. Commit them first (git archive only packs HEAD)." >&2
    exit 1
fi

# A fresh work folder inside dist/ (rebuilt each run; ${VAR:?} stops the script if a name is empty).
OUT="$ROOT/dist"
STAGE="$OUT/$REL_NAME"
rm -rf "${STAGE:?}"
rm -f "${OUT:?}/${REL_NAME:?}.tar.gz"
mkdir -p "$STAGE"

# MicroTeX is a git submodule, and git archive leaves submodules out: it must be checked out at
# the commit this repository records (git submodule status prints "-" when it is missing, "+"
# when it is at another commit).
case "$(git submodule status third_party/microtex)" in
    [-+U]*) echo "third_party/microtex is missing or not at the recorded commit. Run: git submodule update --init" >&2
            exit 1 ;;
esac

# 1. The source tarball that makepkg will build from: this repository's HEAD plus MicroTeX's,
#    joined into one tar (tar -A appends one archive to another), then compressed.
git archive --format=tar --prefix="$SRC_NAME/" -o "$STAGE/$SRC_NAME.tar" HEAD
git -C third_party/microtex archive --format=tar --prefix="$SRC_NAME/third_party/microtex/" \
    -o "$STAGE/microtex.tar" HEAD
tar -Af "$STAGE/$SRC_NAME.tar" "$STAGE/microtex.tar"
rm "$STAGE/microtex.tar"
gzip -n "$STAGE/$SRC_NAME.tar"      # -n: no timestamp inside, so the same commit gives the same checksum

# 2. Safety check: the Evangelion art is copyrighted and must never ship.
if tar -tzf "$STAGE/$SRC_NAME.tar.gz" | grep -E '(^|/)(unit0[0-9][^/]*\.png|src/ui/art/.*\.png)$'; then
    echo "Eva art found in the source tarball (listed above). Refusing." >&2
    exit 1
fi

# 3. The PKGBUILD, with the real checksum of that tarball written in place of SKIP.
SUM="$(sha256sum "$STAGE/$SRC_NAME.tar.gz" | cut -d' ' -f1)"
sed -e "s/^sha256sums=('SKIP')/sha256sums=('$SUM')/" \
    -e "s/^pkgver=.*/pkgver=$VERSION/" \
    packaging/arch/PKGBUILD > "$STAGE/PKGBUILD"
grep -q "$SUM" "$STAGE/PKGBUILD" || { echo "Checksum was not written into the PKGBUILD." >&2; exit 1; }

# 4. The helper files for the end user.
cp packaging/arch/install.sh packaging/arch/README.txt "$STAGE/"
chmod +x "$STAGE/install.sh"

# 5. Pack everything into the release tarball.
tar -C "$OUT" -czf "$OUT/$REL_NAME.tar.gz" "$REL_NAME"
echo "Done: $OUT/$REL_NAME.tar.gz"
echo "sha256 of the source tarball: $SUM"
