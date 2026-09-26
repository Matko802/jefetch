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
          version = "0.1.0";
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
      packages = forAllSystems (pkgs: {
        default = jefetch { pkgs = pkgs; };
        jefetch = jefetch { pkgs = pkgs; };
      });

      overlays.default = final: _prev: {
        jefetch = jefetch { pkgs = final; };
      };

      devShells = forAllSystems (pkgs:
        pkgs.mkShell {
          buildInputs = [ pkgs.gcc pkgs.gnumake ];
        });
    };
}
