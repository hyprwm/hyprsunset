{
  lib,
  stdenv,
  cmake,
  pkg-config,
  hyprland-protocols,
  hyprutils,
  hyprwayland-scanner,
  wayland,
  wayland-protocols,
  wayland-scanner,
  version ? "git",
}: let
  inherit (lib.sources) cleanSource cleanSourceWith;
  inherit (lib.strings) hasSuffix;
in
  stdenv.mkDerivation {
    pname = "hyprsunset";
    inherit version;

    src = cleanSourceWith {
      filter = name: _type: let
        baseName = baseNameOf (toString name);
      in
        ! (hasSuffix ".nix" baseName);
      src = cleanSource ../.;
    };

    nativeBuildInputs = [
      cmake
      pkg-config
      hyprwayland-scanner
    ];

    buildInputs = [
      hyprland-protocols
      hyprutils
      wayland
      wayland-protocols
      wayland-scanner
    ];

    meta = {
      homepage = "https://github.com/hyprwm/hyprsunset";
      description = "An application to enable a blue-light filter on Hyprland";
      license = lib.licenses.bsd3;
      platforms = lib.platforms.linux;
      mainProgram = "hyprsunset";
    };
  }
