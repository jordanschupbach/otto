{
  description = "Otto — a minimal web browser in C++ on WebKitGTK";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.systems.url = "github:nix-systems/default";
  inputs.flake-utils = {
    url = "github:numtide/flake-utils";
    inputs.systems.follows = "systems";
  };

  outputs =
    { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Runtime pieces WebKitGTK needs: TLS (glib-networking) and media (GStreamer).
        runtimeDeps = [
          pkgs.glib-networking
          pkgs.gst_all_1.gst-plugins-base
          pkgs.gst_all_1.gst-plugins-good
          pkgs.gst_all_1.gst-plugins-bad
        ];

        otto = pkgs.stdenv.mkDerivation {
          pname = "otto";
          version = "0.1.0";
          src = self;
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.wrapGAppsHook4
          ];
          buildInputs = [
            pkgs.gtk4
            pkgs.webkitgtk_6_0
            pkgs.libsysprof-capture # referenced by glib-2.0.pc; silences pkg-config errors
          ] ++ runtimeDeps;
          meta.mainProgram = "otto";
        };
      in
      {
        packages.default = otto;

        apps.default = {
          type = "app";
          program = "${otto}/bin/otto";
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ otto ];
          packages = [
            pkgs.clang-tools # clangd, clang-format
            pkgs.gdb
            pkgs.just
          ];
          # Let a dev build (./build/otto) find TLS, icons and GSettings schemas
          # the same way wrapGAppsHook4 does for the packaged binary.
          shellHook = ''
            export GIO_EXTRA_MODULES="${pkgs.glib-networking}/lib/gio/modules''${GIO_EXTRA_MODULES:+:$GIO_EXTRA_MODULES}"
            export XDG_DATA_DIRS="${pkgs.gtk4}/share/gsettings-schemas/${pkgs.gtk4.name}:${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}:${pkgs.adwaita-icon-theme}/share''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
          '';
        };
      }
    );
}
