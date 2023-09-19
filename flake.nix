{
  description = "Nix flake for building the project";

  inputs.nixpkgs.url = "nixpkgs/release-23.05"; # Or any other nixpkgs version

  outputs = { self, nixpkgs }: 
  let
    pkgs = nixpkgs.legacyPackages.x86_64-linux; # Adjust the architecture as needed
    buildInputs = [
        pkgs.gnumake
        pkgs.gcc-arm-embedded
        pkgs.newlib
        pkgs.git
        pkgs.python3
        pkgs.python3Packages.pillow
      ];
  in
  {
    devShell.x86_64-linux = pkgs.mkShell {
      buildInputs = buildInputs;
    };

    packages.x86_64-linux.default = pkgs.stdenv.mkDerivation {
      name = "project";

      src = ./.; # Assumes the current directory contains your project
      buildInputs = buildInputs;
      buildPhase = ''

        patchShebangs ./libopencm3/scripts/
        make -j -C libopencm3
        make -C dpsboot elf bin GIT_VERSION=${if (self ? rev) then self.rev else "dirty"} ROOT=$(pwd)
        make -C opendps fonts GIT_VERSION=${if (self ? rev) then self.rev else "dirty"} ROOT=$(pwd)
        make -C opendps elf bin MODEL=DPS5005 GIT_VERSION=${if (self ? rev) then self.rev else "dirty"} ROOT=$(pwd)
        make -C opendps elf bin MODEL=DPS5015 GIT_VERSION=${if (self ? rev) then self.rev else "dirty"} ROOT=$(pwd)
      '';

      installPhase = ''
        mkdir -p $out
        cp opendps/opendps*.elf $out/
        cp opendps/opendps*.bin $out/
        cp dpsboot/dpsboot.elf $out/
        cp dpsboot/dpsboot.bin $out/
      '';
    };
  };
}

