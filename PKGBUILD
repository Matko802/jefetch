# Maintainer: Matko802 <https://github.com/Matko802>
pkgname=jefetch-git
_pkgname=jefetch
pkgver=0.1.0.r40.g01169ed
pkgrel=1
pkgdesc="The Fastest C Fetcher"
arch=('x86_64' 'aarch64' 'armv7h')
url="https://github.com/Matko802/jefetch"
license=('MIT')
depends=('glibc' 'gcc-libs')
makedepends=('gcc' 'make' 'git')
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
  make
}

package() {
  cd "$_pkgname"
  make DESTDIR="$pkgdir" PREFIX=/usr install
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
