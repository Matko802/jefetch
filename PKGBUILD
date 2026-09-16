# Maintainer: Matko802 <https://github.com/Matko802>
pkgname=jefetch-git
_pkgname=jefetch
pkgver=0.1.0.r40.g01169ed
pkgrel=1
pkgdesc="A fastfetch clone written in pure Rust"
arch=('x86_64' 'aarch64' 'armv7h')
url="https://github.com/Matko802/jefetch"
license=('MIT')
depends=('glibc' 'gcc-libs')
makedepends=('cargo' 'git')
provides=('jefetch')
conflicts=('jefetch')
source=("git+https://github.com/Matko802/jefetch.git")
sha256sums=('SKIP')

pkgver() {
  cd "$_pkgname"
  git describe --long --tags | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g'
}

build() {
  cd "$_pkgname"
  export CARGO_HOME="$srcdir/cargo-home"
  cargo build --release --locked
}

package() {
  cd "$_pkgname"
  install -Dm755 "target/release/jefetch" "$pkgdir/usr/bin/jefetch"
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
