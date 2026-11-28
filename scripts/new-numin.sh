#!/usr/bin/env bash
# Create a new Numin from the template and register it.
#
#   scripts/new-numin.sh Shimmer
source "$(dirname "$0")/_common.sh"

name="${1:-}"
[[ "$name" =~ ^[A-Z][A-Za-z0-9]*$ ]] || { echo "usage: scripts/new-numin.sh Name   (CamelCase, starts with a capital)"; exit 2; }
[ -e "numins/$name" ] && { echo "numins/$name already exists"; exit 1; }

mkdir -p "numins/$name"
sed "s/__NAME__/$name/g" numins/_Template/Template.h > "numins/$name/$name.h"

perl -pi -e "s|^// \@numin-includes|#include \"$name/$name.h\"\n// \@numin-includes|" numins/Registry.cpp
perl -pi -e "s|^(\s*)// \@numin-entries|\1entry<$name>(Status::InTheShop),\n\1// \@numin-entries|" numins/Registry.cpp

echo "Created numins/$name/$name.h and registered it in numins/Registry.cpp."
echo "Try it:  scripts/listen.sh $name samples/<file>.wav --watch"
