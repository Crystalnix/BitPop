WD=`pwd`
BASE_DIR=`dirname $0`
EXT_DIR="../../chrome/browser/extensions/default_extensions"
EXT_DEFS="$EXT_DIR/external_extensions.json"

# extension names list
EXT_NAMES="dropdown_most_visited facebook_controller facebook_friends facebook_messages facebook_notifications uncensor_proxy"

echo "=== Started ==="
echo

cd "$BASE_DIR"
find . -name "*.swl" -o -name "*.swm" -o -name "*.swn" -o -name "*.swo" -o -name "*.swp" -o -name "*.un~" -o -name ".DS_Store" | xargs rm -f

echo "// This json file will contain a list of extensions that will be included" > "$EXT_DEFS"
echo "// in the installer." >> "$EXT_DEFS"
echo "" >> "$EXT_DEFS"
echo "{" >> "$EXT_DEFS"

first_ext="yes"
for EXT in $EXT_NAMES; do
  echo "=== Processing $EXT ..."

  ./crxmake.sh "$EXT/" "$EXT.pem"
  cp -f "$EXT.crx" "$EXT_DIR/"
  EXT_ID=`./extid.rb "$EXT.pem"`
  EXT_VERSION=`grep \"version\": "$EXT/manifest.json" | sed -E 's/[^[:digit:]\.]//g'`
  
  #if [ ! $first_ext ]; then
  #  echo "," >> "$EXT_DEFS"
  #else
  #  first_ext=
  #fi
  echo "  \"$EXT_ID\": {" >> "$EXT_DEFS"
  echo "    \"external_crx\": \"$EXT.crx\"," >> "$EXT_DEFS"
  echo "    \"external_version\": \"$EXT_VERSION\"" >> "$EXT_DEFS"
  #echo -n "  }" >> "$EXT_DEFS"
  echo "  }," >> "$EXT_DEFS"

  echo "... Done ==="
done

echo "  \"nnbmlagghjjcbdhgmkedmbmedengocbn\": {" >> "$EXT_DEFS"
echo "    \"external_crx\": \"docsviewer-extension.crx\"," >> "$EXT_DEFS"
echo "    \"external_version\": \"3.5\"" >> "$EXT_DEFS"
echo "  }" >> "$EXT_DEFS"
echo "}" >> "$EXT_DEFS"

cd "$WD"

echo
echo "=== Finished ==="

