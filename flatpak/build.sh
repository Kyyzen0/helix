#!/usr/bin/env bash
cd "$(dirname "$0")"
flatpak-builder --repo=build/repo --force-clean build/out com.helix3d.Helix.yml
flatpak build-bundle build/repo build/helix.flatpak com.helix3d.Helix
