{
  description = "ld-decode-tools (Nix flake)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    ezpwd = {
      url = "github:pjkundert/ezpwd-reed-solomon";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, ezpwd }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        ezpwdSrc = ezpwd;
      in
      let
        packageVersion = "7.2.0";
        rev = if self ? rev then self.rev else "";
        shortRev = if self ? shortRev then self.shortRev else (if rev != "" then builtins.substring 0 7 rev else "unknown");
        dirtySuffix = if self ? dirtyRev then "-dirty" else "";
        branch = if self ? ref then self.ref else "nix";
        nixCommit = "${shortRev}${dirtySuffix}";
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "ld-decode-tools";
          version = packageVersion;
          src = pkgs.lib.cleanSourceWith {
            src = ./.;
            filter = path: type:
              let
                base = pkgs.lib.baseNameOf path;
              in
                !(base == ".git" || base == "build" || base == "result");
          };

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
            qt6.qtsvg
            fftw
            ffmpeg
            sqlite
            libGL
          ];

          cmakeBuildType = "Release";
          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DEZPWD_DIR=${ezpwdSrc}/c++"
            "-DAPP_BRANCH=${branch}"
            "-DAPP_COMMIT=${nixCommit}"
          ];
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.qtbase
            qt6.qtsvg
            fftw
            ffmpeg
            sqlite
            libGL
            python3
            python3Packages.numpy
          ];
          EZPWD_DIR = "${ezpwdSrc}/c++";
        };
      }
    );
}