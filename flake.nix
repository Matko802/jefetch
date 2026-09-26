{
  description = "jefetch is the Fastest C Fetcher";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f (nixpkgs.legacyPackages.${system}));

      jefetch =
        { pkgs }:
        pkgs.stdenv.mkDerivation {
          pname = "jefetch";
          version = "0.2.15";
          src = pkgs.lib.cleanSource ./.;

          nativeBuildInputs = [ pkgs.gnumake ];

          makeFlags = [ "PREFIX=$(out)" ];

          meta = {
            mainProgram = "jefetch";
            description = "A fastfetch clone written in pure C";
            homepage = "https://github.com/Matko802/jefetch";
            license = nixpkgs.lib.licenses.mit;
            platforms = nixpkgs.lib.platforms.linux;
          };
        };
    in
    {
      packages = forAllSystems (pkgs:
        let
          build = jefetch { pkgs = pkgs.pkgsStatic; };
        in
        {
          default = pkgs.runCommand "jefetch" { } ''
            mkdir -p $out/bin
            install -Dm755 ${build}/bin/jefetch $out/bin/jefetch
          '';
          jefetch = pkgs.runCommand "jefetch" { } ''
            mkdir -p $out/bin
            install -Dm755 ${build}/bin/jefetch $out/bin/jefetch
          '';
        });

      overlays.default = final: _prev: {
        jefetch = jefetch { pkgs = final.pkgsStatic; };
      };

      devShells = forAllSystems (pkgs:
        pkgs.mkShell {
          buildInputs = [ pkgs.gcc pkgs.gnumake ];
        });
    };
}
