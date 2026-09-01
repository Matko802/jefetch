{
  description = "sharkfetch - a fastfetch clone written in pure Rust (static musl)";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f (nixpkgs.legacyPackages.${system}));

      sharkfetch =
        { pkgs }:
        pkgs.rustPlatform.buildRustPackage {
          pname = "sharkfetch";
          version = "0.1.0";
          src = pkgs.lib.cleanSource ./.;

          # Rely on Cargo.lock rather than vendoring dependencies.
          cargoLock.lockFile = ./Cargo.lock;

          # The repo ships a dev-only .cargo/config.toml tuned for the dev
          # box's rustup musl toolchain; it would fight Nix's rustflags, so
          # drop it from the build source.
          preBuild = ''
            rm -f .cargo/config.toml
          '';

          meta = {
            mainProgram = "sharkfetch";
            description = "A fastfetch clone written in pure Rust, statically linked against musl";
            homepage = "https://github.com/Matko802/sharkfetch";
            license = nixpkgs.lib.licenses.mit;
            platforms = nixpkgs.lib.platforms.linux;
          };
        };
    in
    {
      # Fully static musl build: no glibc, no dynamic linking.
      packages = forAllSystems (pkgs:
        let
          staticBuild = sharkfetch { pkgs = pkgs.pkgsStatic; };
        in
        {
          default = pkgs.runCommand "sharkfetch" { } ''
            mkdir -p $out/bin
            install -Dm755 ${staticBuild}/bin/sharkfetch $out/bin/sharkfetch
          '';
          sharkfetch = pkgs.runCommand "sharkfetch" { } ''
            mkdir -p $out/bin
            install -Dm755 ${staticBuild}/bin/sharkfetch $out/bin/sharkfetch
          '';
        });

      overlays.default = final: _prev: {
        sharkfetch = sharkfetch { pkgs = final.pkgsStatic; };
      };

      devShells = forAllSystems (pkgs:
        pkgs.mkShell {
          buildInputs = [ pkgs.pkgsMusl.cargo pkgs.pkgsMusl.rustc ];
        });
    };
}
