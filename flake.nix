{
  description = "tbc-tools (Nix flake)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    nixpkgsLegacy.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
    ezpwd = {
      url = "github:pjkundert/ezpwd-reed-solomon";
      flake = false;
    };
  };
  outputs = { self, nixpkgs, nixpkgsLegacy, flake-utils, ezpwd }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgsUnstable = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };
        legacyPkgs = import nixpkgsLegacy {
          inherit system;
          config.allowUnfree = true;
        };
        ezpwdSrc = ezpwd;
        isLinux = pkgsUnstable.stdenv.hostPlatform.isLinux;
        isDarwin = pkgsUnstable.stdenv.hostPlatform.isDarwin;
        isLinuxX86_64 = isLinux && pkgsUnstable.stdenv.hostPlatform.isx86_64;
        enableCuda = isLinuxX86_64;
        pkgs = if isLinux then legacyPkgs else pkgsUnstable;
        flacPackage = if isLinux then pkgsUnstable.flac else pkgs.flac;
        # Vendor older CUDA package sets from legacy nixpkgs for Pascal/GTX 1000 support.
        # Keep both sets available; default to CUDA 11.8 for pre-Volta compatibility.
        vendoredCudaPackages11 = if enableCuda then legacyPkgs.cudaPackages_11_8 else null;
        vendoredCudaPackages12 = if enableCuda then legacyPkgs.cudaPackages_12_4 else null;
        cudaPackages = vendoredCudaPackages11;
        cudaHostCompiler = if enableCuda then legacyPkgs.gcc11 else null;
        cudaCudnnPackage = if enableCuda then cudaPackages.cudnn_8_9 else null;
        # ORT source policy (hard rule): Microsoft purged the onnxruntime
        # GitHub Releases section on 2026-11-20 (the v1.18.1 and v1.23.2
        # pins both 404'd, breaking every Linux/Windows/macOS CI job), so
        # ORT comes from the immutable nuget.org package registry instead.
        # The nupkgs carry the same build outputs (flat public headers +
        # native libraries) under runtimes/<platform>/native; the install
        # phases arrange them into the include/ + lib/ tree that
        # ONNXRUNTIME_ROOT consumers expect. Never point these back at a
        # GitHub release URL (enforced by ci/check_ci_contracts.py).
        onnxruntimePackage =
          if isLinuxX86_64 then
            legacyPkgs.stdenvNoCC.mkDerivation rec {
              pname = "onnxruntime-gpu-prebuilt";
              version = "1.18.1";
              src = legacyPkgs.fetchurl {
                url = "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.Gpu.Linux/${version}";
                sha256 = "sha256-Nb/PGwmlu9Hc5KA2ykOJWvqXGmrtqg481I+Iosrxazo=";
                # Name the download with a .zip suffix so the default unpack
                # phase recognises the nupkg.
                name = "onnxruntime-gpu-linux-${version}.zip";
              };
              nativeBuildInputs = [ legacyPkgs.unzip ];
              # The nupkg extracts multiple top-level directories (build/,
              # buildTransitive/, runtimes/, ...) so the unpacker cannot
              # auto-pick a source root; stay in the unpack directory.
              sourceRoot = ".";
              dontConfigure = true;
              dontBuild = true;
              installPhase = ''
                runHook preInstall
                mkdir -p "$out/include" "$out/lib"
                cp buildTransitive/native/include/*.h "$out/include/"
                cp runtimes/linux-x64/native/*.so "$out/lib/"
                # The .so records SONAME libonnxruntime.so.${version} but the
                # nupkg ships only the unversioned file name; provide the
                # SONAME name for the dynamic loader.
                ln -s libonnxruntime.so "$out/lib/libonnxruntime.so.${version}"
                cp LICENSE "$out/LICENSE" || true
                runHook postInstall
              '';
            }
          else if isDarwin then
            # Microsoft prebuilt for CoreML EP (nixpkgs lacks it). The fat
            # 1.23.2 nupkg ships the per-arch thin dylibs
            # (runtimes/osx-{arm64,x64}) required for the universal DMG's
            # per-arch lipo merge, plus the public headers.
            let
              archSuffix =
                if pkgsUnstable.stdenv.hostPlatform.isAarch64 then "arm64" else "x86_64";
              # The nupkg's runtimes/ tree uses x64 (not x86_64) for the
              # Intel macOS natives.
              runtimeArch =
                if pkgsUnstable.stdenv.hostPlatform.isAarch64 then "arm64" else "x64";
            in
            pkgsUnstable.stdenvNoCC.mkDerivation rec {
              pname = "onnxruntime-osx-${archSuffix}-prebuilt";
              version = "1.23.2";
              src = pkgsUnstable.fetchurl {
                url = "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime/${version}";
                sha256 = "sha256-Jf4XL3/N808rWwLIuZf2uIq8KClWhJ1K9DNa8j0MTko=";
                name = "onnxruntime-osx-${archSuffix}-${version}.zip";
              };
              # stdenvNoCC omits cctools, so install_name_tool isn't on PATH.
              # autoSignDarwinBinariesHook re-signs ad-hoc in fixupPhase after
              # install_name_tool invalidates Microsoft's signature.
              nativeBuildInputs = [
                pkgsUnstable.cctools
                pkgsUnstable.darwin.autoSignDarwinBinariesHook
                pkgsUnstable.unzip
              ];
              # The nupkg extracts multiple top-level directories (build/,
              # runtimes/, package/, ...) so the unpacker cannot auto-pick a
              # source root; stay in the unpack directory.
              sourceRoot = ".";
              dontConfigure = true;
              dontBuild = true;
              # Rewrite the dylib's install_name to its absolute store path
              # (nix convention for a prebuilt) so we don't need
              # nix-specific LC_RPATH workarounds in the built installables.
              installPhase = ''
                runHook preInstall
                mkdir -p "$out/include" "$out/lib"
                cp build/native/include/*.h "$out/include/"
                cp "runtimes/osx-${runtimeArch}/native/libonnxruntime.dylib" "$out/lib/"
                cp LICENSE "$out/LICENSE" || true
                install_name_tool -id \
                  "$out/lib/libonnxruntime.dylib" \
                  "$out/lib/libonnxruntime.dylib"
                runHook postInstall
              '';
            }
          else
            pkgs.onnxruntime;
        cudaRuntimeDependencies = pkgs.lib.optionals enableCuda [
          cudaPackages.cuda_cudart
          cudaPackages.libcufft
          cudaPackages.libcurand
          cudaPackages.libcublas
          cudaCudnnPackage
        ];
        runtimeLibraryPath = pkgs.lib.optionalString isLinux (pkgs.lib.makeLibraryPath ([ onnxruntimePackage ] ++ cudaRuntimeDependencies));
      in
      let
        # Single source of truth for the tools' version is vcpkg.json (the same
        # source root CMakeLists.txt reads APP_VERSION from when unset); derive
        # it here so the derivation version cannot drift from the tools.
        packageVersion = (builtins.fromJSON (builtins.readFile ./vcpkg.json)).version;
        rev = if self ? rev then self.rev else "";
        shortRev = if self ? shortRev then self.shortRev else (if rev != "" then builtins.substring 0 7 rev else "unknown");
        dirtySuffix = if self ? dirtyRev then "-dirty" else "";
        branch = if self ? ref then self.ref else "nix";
        nixCommit = "${shortRev}${dirtySuffix}";
        # mkTbcTools: single derivation factory. withCuda=false (default release,
        # CI test/release jobs) builds CPU-only - no nvcc, no CUDA buildInputs,
        # -DLDCHROMA_ENABLE_CUDA=OFF - so default `nix build .#` skips CUDA kernel
        # compilation and the pinned CUDA 11.8 closure restore. withCuda=true
        # (packages.cuda, used only by the CUDA-plugin publish job) enables the
        # legacy custom-kernel build + CUDA toolchain. Both still link the ORT GPU
        # prebuilt so the ORT CUDA EP can be loaded at runtime when the CUDA plugin
        # (runtime DLLs/SOs) is present; withCuda=false simply does not *compile*
        # the nnTransform3D_kernel.cu custom kernel (Path 2), consistent with how
        # macOS/non-CUDA builds already behave.
        mkTbcTools = { withCuda }:
          pkgs.stdenv.mkDerivation {
            pname = "tbc-tools";
            version = packageVersion;
            src = pkgs.lib.cleanSourceWith {
              src = ./.;
              filter = path: type:
                let
                  base = builtins.baseNameOf path;
                in
                  !(base == ".git" || base == "build" || base == "result");
            };

            nativeBuildInputs = with pkgs; [
              cmake
              ninja
              pkg-config
              qt6.wrapQtAppsHook
            ] ++ pkgs.lib.optionals (enableCuda && withCuda) [
              cudaPackages.cuda_nvcc
            ];

            buildInputs = with pkgs; [
              qt6.qtbase
              qt6.qtsvg
              fftw
              flacPackage
              ffmpeg
              sqlite
              libGL
              onnxruntimePackage
            ] ++ pkgs.lib.optionals (enableCuda && withCuda) [
              cudaPackages.cudatoolkit
              cudaPackages.cuda_cudart
              cudaPackages.libcufft
              cudaPackages.libcurand
              cudaPackages.libcublas
              cudaCudnnPackage
            ];

            cmakeBuildType = "Release";
            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DEZPWD_DIR=${ezpwdSrc}/c++"
              "-DAPP_BRANCH=${branch}"
              "-DAPP_COMMIT=${nixCommit}"
              "-DLDCHROMA_ENABLE_CUDA=${if withCuda then "ON" else "OFF"}"
            ] ++ pkgs.lib.optionals (isLinux || isDarwin) [
              "-DONNXRUNTIME_ROOT=${onnxruntimePackage}"
            ] ++ pkgs.lib.optionals (enableCuda && withCuda) [
              "-DCUDAToolkit_ROOT=${cudaPackages.cudatoolkit}"
              "-DCMAKE_CUDA_HOST_COMPILER=${cudaHostCompiler}/bin/g++"
            ];

            # Self-contained ffmpeg policy (hard rule): a deployment must
            # never depend on a host/system ffmpeg. The flake-pinned ffmpeg
            # (7.1.1, threaded filter graphs) is symlinked into $out/bin next
            # to the tbc-video-export wrapper so `nix profile install .#`
            # ships it; tbc-video-export's find_binary resolves the
            # script-adjacent binary before PATH, and tbc-analyse's
            # app-dir-first executable search finds it the same way. The
            # AppImage/tar release jobs replace these symlinks with real
            # copies during bundle assembly. Enforced by
            # ci/check_ci_contracts.py (FFMPEG_SELF_CONTAINED_*).
            postInstall = ''
              ln -s ${pkgs.ffmpeg.bin}/bin/ffmpeg $out/bin/ffmpeg
              ln -s ${pkgs.ffmpeg.bin}/bin/ffprobe $out/bin/ffprobe
              ln -s ${pkgs.ffmpeg.bin}/bin/ffplay $out/bin/ffplay
            '';
          };
      in
      assert pkgs.lib.assertMsg
        (!enableCuda || pkgs.lib.versionAtLeast cudaPackages.cudatoolkit.version "11.8")
        "CUDA toolkit must be >= 11.8";
      assert pkgs.lib.assertMsg
        (!enableCuda || pkgs.lib.versionAtLeast vendoredCudaPackages12.cudatoolkit.version "12.4")
        "Vendored CUDA 12 package set must provide toolkit >= 12.4";
      # Pre-empt the Nixpkgs 25.05 removal of cudaPackages_11_8: if the
      # nixpkgsLegacy pin is ever bumped past a rev that drops it (or drops
      # cudnn_8_9 / gcc11), fail loudly at eval time instead of silently
      # breaking GTX-1000-series (Pascal) CUDA builds.
      assert pkgs.lib.assertMsg
        (!enableCuda || legacyPkgs ? cudaPackages_11_8)
        "nixpkgsLegacy no longer provides cudaPackages_11_8. Re-pin nixpkgsLegacy to a Nixpkgs rev that still ships it (e.g. nixos-24.11) to preserve GTX-1000-series CUDA support.";
      assert pkgs.lib.assertMsg
        (!enableCuda || (cudaPackages != null && cudaPackages ? cudatoolkit && cudaPackages ? cudnn_8_9 && legacyPkgs ? gcc11))
        "Vendored CUDA 11.8 package set is incomplete (missing cudatoolkit, cudnn_8_9, or gcc11). The nixpkgsLegacy pin may have drifted.";
      assert pkgs.lib.assertMsg
        (!enableCuda || pkgs.lib.hasPrefix "11.8" cudaPackages.cudatoolkit.version)
        "CUDA toolkit must be 11.8.x for GTX-1000-series (Pascal) support — got ${cudaPackages.cudatoolkit.version}";
      {
        packages.ffmpeg = pkgs.ffmpeg;
        packages.default = mkTbcTools { withCuda = false; };
        # CUDA-enabled build (legacy custom kernel + CUDA toolchain). Only
        # available where the pinned CUDA 11.8 closure exists (Linux x86_64);
        # used by the CUDA-plugin publish CI job, not by default releases.
        packages.cuda = if enableCuda then mkTbcTools { withCuda = true; } else null;
        # Staged CUDA 11.8 runtime + cuDNN 8.9 .so files for the Linux x86_64
        # CUDA plugin package. cuDNN 8.9 has no Linux PyPI wheel (only win_amd64),
        # so the Linux plugin package sources its .so files from the Nix store
        # (the pinned cudaPackages_11_8 / cudnn_8_9). The publish CI job does
        # `nix build .#cuda-plugin-linux-deps` and passes the output to
        # scripts/cuda-plugin-package.sh build-linux --deps-dir. Only the ORT
        # CUDA EP provider .so is fetched separately (from the ORT GPU prebuilt).
        packages.cuda-plugin-linux-deps =
          if enableCuda
          then pkgs.runCommand "cuda-plugin-linux-deps" { } ''
            mkdir -p $out
            cp ${cudaPackages.cuda_cudart.lib}/lib/libcudart.so.11.0 $out/
            cp ${cudaPackages.libcublas.lib}/lib/libcublas.so.11 $out/
            cp ${cudaPackages.libcublas.lib}/lib/libcublasLt.so.11 $out/
            cp ${cudaPackages.libcufft.lib}/lib/libcufft.so.10 $out/
            cp ${cudaPackages.libcurand.lib}/lib/libcurand.so.10 $out/
            cp ${cudaCudnnPackage.lib}/lib/libcudnn.so.8 $out/
            cp ${cudaCudnnPackage.lib}/lib/libcudnn_cnn_infer.so.8 $out/
            cp ${cudaCudnnPackage.lib}/lib/libcudnn_ops_infer.so.8 $out/
          ''
          else null;

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.qtbase
            qt6.qtsvg
            fftw
            flacPackage
            ffmpeg
            sqlite
            libGL
            python3
            python3Packages.numpy
            python3Packages.scipy
            python3Packages.matplotlib
            python3Packages.click
            python3Packages.tqdm
            python3Packages.pyzmq
            python3Packages.watchdog
            python3Packages.pyserial
            onnxruntimePackage
          ] ++ pkgs.lib.optionals enableCuda [
            cudaPackages.cudatoolkit
            cudaPackages.cuda_nvcc
            cudaPackages.cuda_cudart
            cudaPackages.libcufft
            cudaPackages.libcurand
            cudaPackages.libcublas
            cudaCudnnPackage
            python3Packages.pycuda
            python3Packages.pyopencl
            ocl-icd
            pocl
            clinfo
          ];
          EZPWD_DIR = "${ezpwdSrc}/c++";
          ONNXRUNTIME_ROOT = "${onnxruntimePackage}";
          CUDAToolkit_ROOT = pkgs.lib.optionalString enableCuda "${cudaPackages.cudatoolkit}";
          CUDA_PATH = pkgs.lib.optionalString enableCuda "${cudaPackages.cudatoolkit}";
          shellHook = pkgs.lib.optionalString enableCuda ''
            export CUDATOOLKIT_ROOT="$CUDAToolkit_ROOT"
            export CUDAHOSTCXX="${cudaHostCompiler}/bin/g++"
            export CMAKE_CUDA_HOST_COMPILER="${cudaHostCompiler}/bin/g++"
            for nvidiaCudaLib in \
              /usr/lib/x86_64-linux-gnu/libcuda.so.1 \
              /usr/lib64/libcuda.so.1 \
              /usr/lib/libcuda.so.1; do
              if [ -f "$nvidiaCudaLib" ]; then
                if [ -n "$LD_PRELOAD" ]; then
                  export LD_PRELOAD="$nvidiaCudaLib:$LD_PRELOAD"
                else
                  export LD_PRELOAD="$nvidiaCudaLib"
                fi
                break
              fi
            done
            if [ -n "$CUDAHOSTCXX" ]; then
              if [ -n "$NVCC_PREPEND_FLAGS" ]; then
                export NVCC_PREPEND_FLAGS="-ccbin $CUDAHOSTCXX $NVCC_PREPEND_FLAGS"
              else
                export NVCC_PREPEND_FLAGS="-ccbin $CUDAHOSTCXX"
              fi
            fi
            export OZ_OPENCL_VENDOR_DIR="$(mktemp -d -t tbc-opencl-vendors-XXXXXX)"
            for icdFile in "${pkgs.pocl}/etc/OpenCL/vendors/"*.icd; do
              [ -f "$icdFile" ] && cp -f "$icdFile" "$OZ_OPENCL_VENDOR_DIR/"
            done
            for nvidiaOpenclLib in \
              /usr/lib/x86_64-linux-gnu/libnvidia-opencl.so.1 \
              /usr/lib64/libnvidia-opencl.so.1 \
              /usr/lib/libnvidia-opencl.so.1; do
              if [ -f "$nvidiaOpenclLib" ]; then
                printf "%s\n" "$nvidiaOpenclLib" > "$OZ_OPENCL_VENDOR_DIR/nvidia-abs.icd"
                break
              fi
            done
            export OCL_ICD_VENDORS="$OZ_OPENCL_VENDOR_DIR"
            export OPENCL_VENDOR_PATH="$OCL_ICD_VENDORS"
            export LD_LIBRARY_PATH="${pkgs.ocl-icd}/lib:${runtimeLibraryPath}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
          '';
        };
      }
    );
}