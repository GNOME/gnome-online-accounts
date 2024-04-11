#!/usr/bin/env bash

srcdirs="src/daemon src/goabackend src/goaidentity"
datadirs="data"

# find source files that contain gettext keywords
files=$(grep -lR --include='*.c' '\(gettext\|[^_)]_\)(' $srcdirs)

# find GSettings schemas that contain gettext-domain string
files="$files "$(grep -lRi --include='*.gschema.xml' 'gettext-domain="[^"]\+"' $datadirs)

# find .desktop files
files="$files "$(find $datadirs -name '*.desktop*')

# filter out excluded files
if [ -f po/POTFILES.skip ]; then
  files=$(for f in $files; do ! grep -q ^$f po/POTFILES.skip && echo $f; done)
fi

# find those that aren't listed in POTFILES.in
missing=$(for f in $files; do ! grep -q ^$f po/POTFILES.in && echo $f; done)

if [ ${#missing} -eq 0 ]; then
  exit 0
fi

cat >&2 <<EOT

The following files are missing from po/POTFILES.po:

EOT
for f in $missing; do
  echo "  $f" >&2
done
echo >&2

exit 1
