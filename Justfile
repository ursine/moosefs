# Build on the Ubuntu release and architecture used by the recipient.
# Uses upstream debian/control, rules, install manifests and maintainer scripts.

default:
    @just --list

# Install packaging tools and the dependencies declared by upstream (requires sudo).
deps:
    #!/usr/bin/env bash
    set -euo pipefail
    sudo apt-get update
    sudo apt-get install -y build-essential debhelper devscripts equivs fakeroot pkg-config systemd
    depdir=$(mktemp -d)
    trap 'rm -rf -- "$depdir"' EXIT
    cp debian/control "$depdir/control"
    cd "$depdir"
    sudo mk-build-deps --install --remove --tool 'apt-get -y --no-install-recommends' control

# Build all seven .debs for systemd systems using upstream's conversion script.
deb:
    #!/usr/bin/env bash
    set -euo pipefail
    for tool in dpkg-buildpackage dpkg-checkbuilddeps dpkg-deb fakeroot tar pkg-config; do
        command -v "$tool" >/dev/null || { echo "Missing $tool; run just deps" >&2; exit 1; }
    done
    dpkg-checkbuilddeps
    unitdir=$(pkg-config --variable=systemdsystemunitdir systemd)
    [[ "$unitdir" == /* ]] || { echo 'Missing systemd pkg-config data; run just deps' >&2; exit 1; }
    source_dir=$PWD
    mkdir -p "$source_dir/dist"
    builddir=$(mktemp -d)
    trap 'rm -rf -- "$builddir"' EXIT
    mkdir "$builddir/source"
    # Include working-tree edits, but keep all build mutations out of the checkout.
    tar --exclude='./.git' --exclude='./dist' -cf - . | tar -xf - -C "$builddir/source"
    cd "$builddir/source"
    ./debian_sysv_to_systemd.sh
    dpkg-buildpackage -b -us -uc -rfakeroot
    # Check every package declared by upstream before publishing the output directory.
    while read -r package; do
        found=false
        for deb in "$builddir"/"${package}"_*.deb; do
            [[ -f "$deb" ]] || continue
            [[ $(dpkg-deb -f "$deb" Package) == "$package" ]]
            dpkg-deb --info "$deb" >/dev/null
            found=true
        done
        "$found" || { echo "Missing package: $package" >&2; exit 1; }
    done < <(sed -n 's/^Package: //p' debian/control)
    outdir=$(mktemp -d "$source_dir/dist/debs-systemd-XXXXXXXX")
    cp "$builddir"/*.deb "$builddir"/*.changes "$builddir"/*.buildinfo "$outdir/"
    cd "$outdir"
    sha256sum ./*.deb > SHA256SUMS
    printf '\nPackages: %s\n' "$outdir"
    printf 'Install selected packages with sudo dpkg -i <package.deb> ...\n'
    printf 'dpkg does not fetch dependencies; sudo apt install ./<package.deb> resolves them.\n'
