#!/bin/bash
# builds Ryoku's Go programs for Windows from dotfiles/ryoku-go-win (node ryoku-go-port.js first)
# usage: ryoku-go-build.sh [module-folder ...]   e.g. hub/backend shell/ipc
export PATH=/c/Users/Administrator/go-sdk/go/bin:$PATH GOTOOLCHAIN=local GOFLAGS=-mod=vendor
export GOCACHE=/c/Users/Administrator/go-sdk/cache GOPATH=/c/Users/Administrator/go-sdk/path CGO_ENABLED=0
D=/c/Users/Administrator/Desktop/CaelestiaWin/dotfiles; OUT=$D/ryoku-go-build; mkdir -p $OUT
declare -A NAME=([wm/hyprland]=ryoku-wm-hyprland [hub/backend]=ryoku-hub [shell/ipc]=ryoku-shell [shell/ryogami/daemon]=ryogami [apps/ryostore/backend]=ryostore [rashin/backend]=ryoku-rashin [cli]=ryoku [palette-bridge]=ryoku-palette-bridge)
for m in ${@:-wm/hyprland hub/backend shell/ipc shell/ryogami/daemon apps/ryostore/backend rashin/backend cli palette-bridge}; do
  echo "=== $m"
  (cd $D/ryoku-go-win/$m && go build -gcflags=-e -ldflags="-s -w" -o $OUT/${NAME[$m]}.exe . 2>&1 | head -${LINES_MAX:-60}) && [ -f $OUT/${NAME[$m]}.exe ] && echo "built ${NAME[$m]}.exe"
done
