pkgname=tshell-git
pkgver=0.0.0.r0.g0000000
pkgrel=1
pkgdesc="TShell — a modular, extensible custom shell (git version)"
arch=('x86_64')
url="https://github.com/EsetrixArc/TShell"
license=('MIT')

depends=('gcc-libs' 'readline')
makedepends=('git' 'gcc' 'make')

source=("git+$url.git")
sha256sums=('SKIP')

pkgver() {
    cd "$srcdir/TShell"
    git describe --long --tags 2>/dev/null || printf "0.0.0.r%s.g%s" \
        "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    cd "$srcdir/TShell"
    make
    make tshc
    make mods
}

package() {
    cd "$srcdir/TShell"

    install -Dm755 TShell "$pkgdir/usr/bin/tsh"
    install -Dm755 tshc     "$pkgdir/usr/bin/tshc"

    for dir in Bin/Mods/*/; do
        [ -d "$dir" ] || continue
        modname="$(basename "$dir")"

        install -d "$pkgdir/usr/lib/tshell/mods/$modname"

        [ -f "$dir/main.so" ] && \
            install -m755 "$dir/main.so" "$pkgdir/usr/lib/tshell/mods/$modname/main.so"

        [ -f "$dir/manifest.json" ] && \
            install -m644 "$dir/manifest.json" "$pkgdir/usr/lib/tshell/mods/$modname/manifest.json"
    done

    # Install API headers to /usr/include/TSh/  (#include <TSh/ModdingAPI.hpp>)
    install -d "$pkgdir/usr/include/TSh"
    install -Dm644 Bin/API/ModdingAPI.hpp       "$pkgdir/usr/include/TSh/ModdingAPI.hpp"
    install -Dm644 Bin/API/DependencyLoader.hpp "$pkgdir/usr/include/TSh/DependencyLoader.hpp"

    # Install system themes to /usr/share/tshell/themes/
    if [ -d "Bin/Themes" ]; then
        for theme in Bin/Themes/*; do
            [ -e "$theme" ] || continue
            install -Dm644 "$theme" "$pkgdir/usr/share/tshell/themes/$(basename "$theme")"
        done
    fi

    install -Dm644 tshcfg.example "$pkgdir/usr/share/tshell/tshcfg.example"
}