WD=`pwd`
BASE_DIR=`dirname $0`
EXT_DIR="../../chrome/browser/extensions/default_extensions"
EXT_DEFS="$EXT_DIR/external_extensions.json"
UPLOAD_DIR="./upload"
BASE_URL="http://tools.bitpop.com/ext"
UPDATES_XML_PATH="$UPLOAD_DIR/updates.xml"
PRODVERSIONMIN_PATH="./prodversionmin.csv"

# extension names list
EXT_NAMES="dropdown_most_visited facebook_controller facebook_friends facebook_messages facebook_notifications uncensor_domains uncensor_proxy"

make_app_entry() {
  # $1 - extension id,
  # $2 - extension version
  # $3 - extension filename (without extension and version suffix)

  LAST_APP_ENTRY="<app appid='$1'>"
  while read line
  do
    IFS=, read id ext_ver min_prod_ver junk_ < <(echo "$line")
    if [ "$id" == "$1" ]; then

      if [ "$ext_ver" == "-" ]; then
        ext_ver="$2"
      fi

      LAST_APP_ENTRY=$LAST_APP_ENTRY"\n  <updatecheck codebase='$BASE_URL/$3-$ext_ver.crx' version='$ext_ver' prodversionmin='$min_prod_ver'/>"
    fi
  done < <(awk 'NR > 1' "$PRODVERSIONMIN_PATH")

  LAST_APP_ENTRY=$LAST_APP_ENTRY"\n</app>"

  return 0
}

echo "=== Started ==="
echo

cd "$BASE_DIR"
[ -d "$UPLOAD_DIR" ] || mkdir "$UPLOAD_DIR"
echo -e "<?xml version='1.0' encoding='UTF-8'?>\n<gupdate xmlns='http://www.google.com/update2/response' protocol='2.0'>" > "$UPDATES_XML_PATH"

find . -name "*.swl" -o -name "*.swm" -o -name "*.swn" -o -name "*.swo" -o -name "*.swp" -o -name "*.un~" -o -name ".DS_Store" | xargs rm -f

echo "// This json file will contain a list of extensions that will be included" > "$EXT_DEFS"
echo "// in the installer." >> "$EXT_DEFS"
echo "" >> "$EXT_DEFS"
echo "{" >> "$EXT_DEFS"

for EXT in $EXT_NAMES; do
  echo "=== Processing $EXT ..."

  ./crxmake.sh "$EXT/" "$EXT.pem"
  cp -f "$EXT.crx" "$EXT_DIR/"

  EXT_ID=`./extid.rb "$EXT.pem"`
  EXT_VERSION=`grep \"version\": "$EXT/manifest.json" | sed -E 's/[^[:digit:]\.]//g'`

  echo "  \"$EXT_ID\": {" >> "$EXT_DEFS"
  echo "    \"external_crx\": \"$EXT.crx\"," >> "$EXT_DEFS"
  echo "    \"external_version\": \"$EXT_VERSION\"" >> "$EXT_DEFS"
  echo "  }," >> "$EXT_DEFS"

  cp -f "$EXT.crx" "$UPLOAD_DIR/$EXT-$EXT_VERSION.crx"

  make_app_entry $EXT_ID $EXT_VERSION $EXT
  echo -e "$LAST_APP_ENTRY" >> "$UPDATES_XML_PATH"

  echo "... Done ==="
done

echo "  \"nnbmlagghjjcbdhgmkedmbmedengocbn\": {" >> "$EXT_DEFS"
echo "    \"external_crx\": \"docsviewer-extension.crx\"," >> "$EXT_DEFS"
echo "    \"external_version\": \"3.5\"" >> "$EXT_DEFS"
echo "  }," >> "$EXT_DEFS"
echo "  \"geoplninmkljnhklaihoejihlogghapi\": {" >> "$EXT_DEFS"
echo "    \"external_crx\": \"share_button.crx\"," >> "$EXT_DEFS"
echo "    \"external_version\": \"0.2\"" >> "$EXT_DEFS"
echo "  }" >> "$EXT_DEFS"
echo "}" >> "$EXT_DEFS"

echo "</gupdate>" >> "$UPDATES_XML_PATH"

cd "$WD"

echo
echo "=== Finished ==="

