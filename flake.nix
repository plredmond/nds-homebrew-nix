{
  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-22.05;
    devkitnix.url = github:knarkzel/devkitnix;
    devkitnix.inputs.nixpkgs.follows = "nixpkgs";
    flake-utils.url = github:numtide/flake-utils;
  };

  outputs = { self, nixpkgs, devkitnix, flake-utils }: flake-utils.lib.eachSystem [ "x86_64-linux" ] (system:
    with (import nixpkgs { inherit system; });
    let
      devkitARM = devkitnix.packages."${system}".devkitARM;
      libc = pkgs.writeText "libc.txt" ''
        include_dir=${devkitARM}/devkitARM/arm-none-eabi/include
        sys_include_dir=${devkitARM}/devkitARM/arm-none-eabi/include
        crt_dir=${devkitARM}/devkitARM/lib
        msvc_lib_dir=
        kernel32_lib_dir=
        gcc_dir=
      '';
    in
    {
      devShells.default = mkShell {
        buildInputs = [
          devkitARM
          desmume
          melonDS
        ];
        shellHook = ''
          export DEVKITPRO=${devkitARM}
          export DEVKITARM=${devkitARM}/devkitARM
          export LIBC=${libc}
          export PATH=$PATH:$DEVKITPRO/tools/bin:$DEVKITARM/bin
        '';
      };
    });
}
