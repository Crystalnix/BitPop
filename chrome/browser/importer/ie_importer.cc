// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/importer/ie_importer.h"

#include <ole2.h>
#include <intshcut.h>
#include <pstore.h>
#include <shlobj.h>
#include <urlhist.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/string16.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/win/registry.h"
#include "base/win/scoped_co_mem.h"
#include "base/win/scoped_com_initializer.h"
#include "base/win/scoped_comptr.h"
#include "base/win/scoped_handle.h"
#include "base/win/windows_version.h"
#include "chrome/browser/importer/importer_bridge.h"
#include "chrome/browser/importer/importer_data_types.h"
#include "chrome/browser/password_manager/ie7_password.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_prepopulate_data.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/common/time_format.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "webkit/forms/password_form.h"

namespace {

// Registry key paths from which we import IE settings.
const char16 kStorage2Path[] =
  L"Software\\Microsoft\\Internet Explorer\\IntelliForms\\Storage2";
const char16 kSearchScopePath[] =
  L"Software\\Microsoft\\Internet Explorer\\SearchScopes";
const char16 kIESettingsMain[] =
  L"Software\\Microsoft\\Internet Explorer\\Main";
const char16 kIEFavoritesOrderKey[] =
  L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
  L"MenuOrder\\Favorites";
const char16 kIEVersionKey[] =
  L"Software\\Microsoft\\Internet Explorer";
const char16 kIEToolbarKey[] =
  L"Software\\Microsoft\\Internet Explorer\\Toolbar";

// A struct that hosts the information of AutoComplete data in PStore.
struct AutoCompleteInfo {
  string16 key;
  std::vector<string16> data;
  bool is_url;
};

// Gets the creation time of the given file or directory.
base::Time GetFileCreationTime(const string16& file) {
  base::Time creation_time;
  base::win::ScopedHandle file_handle(
      CreateFile(file.c_str(), GENERIC_READ,
                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                 NULL, OPEN_EXISTING,
                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL));
  FILETIME creation_filetime;
  if (GetFileTime(file_handle, &creation_filetime, NULL, NULL))
    creation_time = base::Time::FromFileTime(creation_filetime);
  return creation_time;
}

// Safely read an object of type T from a raw sequence of bytes.
template<typename T>
bool BinaryRead(T* data, size_t offset, const std::vector<uint8>& blob) {
  if (offset + sizeof(T) > blob.size())
    return false;
  memcpy(data, &blob[offset], sizeof(T));
  return true;
}

// Safely read an ITEMIDLIST from a raw sequence of bytes.
//
// An ITEMIDLIST is a list of SHITEMIDs, terminated by a SHITEMID with
// .cb = 0. Here, before simply casting &blob[offset] to LPITEMIDLIST,
// we verify that the list structure is not overrunning the boundary of
// the binary blob.
LPCITEMIDLIST BinaryReadItemIDList(size_t offset, size_t idlist_size,
                                   const std::vector<uint8>& blob) {
  size_t head = 0;
  while (true) {
    SHITEMID id;
    if (head >= idlist_size || !BinaryRead(&id, offset + head, blob))
      return NULL;
    if (id.cb == 0)
      break;
    head += id.cb;
  }
  return reinterpret_cast<LPCITEMIDLIST>(&blob[offset]);
}

// Compares the two bookmarks in the order of IE's Favorites menu.
// Returns true if rhs should come later than lhs (lhs < rhs).
struct IEOrderBookmarkComparator {
  bool operator()(const ProfileWriter::BookmarkEntry& lhs,
                  const ProfileWriter::BookmarkEntry& rhs) const {
    static const uint32 kNotSorted = 0xfffffffb; // IE uses this magic value.
    FilePath lhs_prefix;
    FilePath rhs_prefix;
    for (size_t i = 0; i <= lhs.path.size() && i <= rhs.path.size(); ++i) {
      const FilePath::StringType lhs_i =
        (i < lhs.path.size() ? lhs.path[i] : lhs.title + L".url");
      const FilePath::StringType rhs_i =
        (i < rhs.path.size() ? rhs.path[i] : rhs.title + L".url");
      lhs_prefix = lhs_prefix.Append(lhs_i);
      rhs_prefix = rhs_prefix.Append(rhs_i);
      if (lhs_i == rhs_i)
        continue;
      // The first path element that differs between the two.
      std::map<FilePath, uint32>::const_iterator lhs_iter =
        sort_index_->find(lhs_prefix);
      std::map<FilePath, uint32>::const_iterator rhs_iter =
        sort_index_->find(rhs_prefix);
      uint32 lhs_sort_index = (lhs_iter == sort_index_->end() ? kNotSorted
        : lhs_iter->second);
      uint32 rhs_sort_index = (rhs_iter == sort_index_->end() ? kNotSorted
        : rhs_iter->second);
      if (lhs_sort_index != rhs_sort_index)
        return lhs_sort_index < rhs_sort_index;
      // If they have the same sort order, sort alphabetically.
      return lhs_i < rhs_i;
    }
    return lhs.path.size() < rhs.path.size();
  }
  const std::map<FilePath, uint32>* sort_index_;
};

// IE stores the order of the Favorites menu in registry under:
// HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\MenuOrder\Favorites.
// The folder hierarchy of Favorites menu is directly mapped to the key
// hierarchy in the registry.
//
// If the order of the items in a folder is customized by user, the order is
// recorded in the REG_BINARY value named "Order" of the corresponding key.
// The content of the "Order" value is a raw binary dump of an array of the
// following data structure
//   struct {
//     uint32 size;  // Note that ITEMIDLIST is variably-sized.
//     uint32 sort_index;  // 0 means this is the first item, 1 the second, ...
//     ITEMIDLIST item_id;
//   };
// where each item_id should correspond to a favorites link file (*.url) in
// the current folder.
bool ParseFavoritesOrderBlob(
    const Importer* importer,
    const std::vector<uint8>& blob,
    const FilePath& path,
    std::map<FilePath, uint32>* sort_index) WARN_UNUSED_RESULT {
  static const int kItemCountOffset = 16;
  static const int kItemListStartOffset = 20;

  // Read the number of items.
  uint32 item_count = 0;
  if (!BinaryRead(&item_count, kItemCountOffset, blob))
    return false;

  // Traverse over the items.
  size_t base_offset = kItemListStartOffset;
  for (uint32 i = 0; i < item_count && !importer->cancelled(); ++i) {
    static const int kSizeOffset = 0;
    static const int kSortIndexOffset = 4;
    static const int kItemIDListOffset = 8;

    // Read the size (number of bytes) of the current item.
    uint32 item_size = 0;
    if (!BinaryRead(&item_size, base_offset + kSizeOffset, blob) ||
        base_offset + item_size <= base_offset || // checking overflow
        base_offset + item_size > blob.size())
      return false;

    // Read the sort index of the current item.
    uint32 item_sort_index = 0;
    if (!BinaryRead(&item_sort_index, base_offset + kSortIndexOffset, blob))
      return false;

    // Read the file name from the ITEMIDLIST structure.
    LPCITEMIDLIST idlist = BinaryReadItemIDList(
      base_offset + kItemIDListOffset, item_size - kItemIDListOffset, blob);
    TCHAR item_filename[MAX_PATH];
    if (!idlist || FAILED(SHGetPathFromIDList(idlist, item_filename)))
      return false;
    FilePath item_relative_path =
      path.Append(FilePath(item_filename).BaseName());

    // Record the retrieved information and go to the next item.
    sort_index->insert(std::make_pair(item_relative_path, item_sort_index));
    base_offset += item_size;
  }
  return true;
}

bool ParseFavoritesOrderRegistryTree(
    const Importer* importer,
    const base::win::RegKey& key,
    const FilePath& path,
    std::map<FilePath, uint32>* sort_index) WARN_UNUSED_RESULT {
  // Parse the order information of the current folder.
  DWORD blob_length = 0;
  if (key.ReadValue(L"Order", NULL, &blob_length, NULL) == ERROR_SUCCESS) {
    std::vector<uint8> blob(blob_length);
    if (blob_length > 0 &&
        key.ReadValue(L"Order", reinterpret_cast<DWORD*>(&blob[0]),
                      &blob_length, NULL) == ERROR_SUCCESS) {
      if (!ParseFavoritesOrderBlob(importer, blob, path, sort_index))
        return false;
    }
  }

  // Recursively parse subfolders.
  for (base::win::RegistryKeyIterator child(key.Handle(), L"");
       child.Valid() && !importer->cancelled();
       ++child) {
    base::win::RegKey subkey(key.Handle(), child.Name(), KEY_READ);
    if (subkey.Valid()) {
      FilePath subpath(path.Append(child.Name()));
      if (!ParseFavoritesOrderRegistryTree(importer, subkey, subpath,
                                           sort_index)) {
        return false;
      }
    }
  }
  return true;
}

bool ParseFavoritesOrderInfo(
    const Importer* importer,
    std::map<FilePath, uint32>* sort_index) WARN_UNUSED_RESULT {
  base::win::RegKey key(HKEY_CURRENT_USER, kIEFavoritesOrderKey, KEY_READ);
  if (!key.Valid())
    return false;
  return ParseFavoritesOrderRegistryTree(importer, key, FilePath(), sort_index);
}

// Read the sort order from registry. If failed, we don't touch the list
// and use the default (alphabetical) order.
void SortBookmarksInIEOrder(
    const Importer* importer,
    std::vector<ProfileWriter::BookmarkEntry>* bookmarks) {
  std::map<FilePath, uint32> sort_index;
  if (!ParseFavoritesOrderInfo(importer, &sort_index))
    return;
  IEOrderBookmarkComparator compare = {&sort_index};
  std::sort(bookmarks->begin(), bookmarks->end(), compare);
}

}  // namespace

// static
// {E161255A-37C3-11D2-BCAA-00C04fD929DB}
const GUID IEImporter::kPStoreAutocompleteGUID = {
    0xe161255a, 0x37c3, 0x11d2,
    { 0xbc, 0xaa, 0x00, 0xc0, 0x4f, 0xd9, 0x29, 0xdb }
};
// {A79029D6-753E-4e27-B807-3D46AB1545DF}
const GUID IEImporter::kUnittestGUID = {
    0xa79029d6, 0x753e, 0x4e27,
    { 0xb8, 0x7, 0x3d, 0x46, 0xab, 0x15, 0x45, 0xdf }
};

IEImporter::IEImporter() {
}

void IEImporter::StartImport(const importer::SourceProfile& source_profile,
                             uint16 items,
                             ImporterBridge* bridge) {
  bridge_ = bridge;
  source_path_ = source_profile.source_path;

  bridge_->NotifyStarted();

  // Some IE settings (such as Protected Storage) are obtained via COM APIs.
  base::win::ScopedCOMInitializer com_initializer;

  if ((items & importer::HOME_PAGE) && !cancelled())
    ImportHomepage();  // Doesn't have a UI item.
  // The order here is important!
  if ((items & importer::HISTORY) && !cancelled()) {
    bridge_->NotifyItemStarted(importer::HISTORY);
    ImportHistory();
    bridge_->NotifyItemEnded(importer::HISTORY);
  }
  if ((items & importer::FAVORITES) && !cancelled()) {
    bridge_->NotifyItemStarted(importer::FAVORITES);
    ImportFavorites();
    bridge_->NotifyItemEnded(importer::FAVORITES);
  }
  if ((items & importer::SEARCH_ENGINES) && !cancelled()) {
    bridge_->NotifyItemStarted(importer::SEARCH_ENGINES);
    ImportSearchEngines();
    bridge_->NotifyItemEnded(importer::SEARCH_ENGINES);
  }
  if ((items & importer::PASSWORDS) && !cancelled()) {
    bridge_->NotifyItemStarted(importer::PASSWORDS);
    // Always import IE6 passwords.
    ImportPasswordsIE6();

    if (CurrentIEVersion() >= 7)
      ImportPasswordsIE7();
    bridge_->NotifyItemEnded(importer::PASSWORDS);
  }
  bridge_->NotifyEnded();
}

IEImporter::~IEImporter() {
}

void IEImporter::ImportFavorites() {
  FavoritesInfo info;
  if (!GetFavoritesInfo(&info))
    return;

  BookmarkVector bookmarks;
  ParseFavoritesFolder(info, &bookmarks);

  if (!bookmarks.empty() && !cancelled()) {
    const string16& first_folder_name =
        l10n_util::GetStringUTF16(IDS_BOOKMARK_GROUP_FROM_IE);
    bridge_->AddBookmarks(bookmarks, first_folder_name);
  }
}

void IEImporter::ImportHistory() {
  const std::string kSchemes[] = {chrome::kHttpScheme,
                                  chrome::kHttpsScheme,
                                  chrome::kFtpScheme,
                                  chrome::kFileScheme};
  int total_schemes = arraysize(kSchemes);

  base::win::ScopedComPtr<IUrlHistoryStg2> url_history_stg2;
  HRESULT result;
  result = url_history_stg2.CreateInstance(CLSID_CUrlHistory, NULL,
                                           CLSCTX_INPROC_SERVER);
  if (FAILED(result))
    return;
  base::win::ScopedComPtr<IEnumSTATURL> enum_url;
  if (SUCCEEDED(result = url_history_stg2->EnumUrls(enum_url.Receive()))) {
    std::vector<history::URLRow> rows;
    STATURL stat_url;
    ULONG fetched;
    while (!cancelled() &&
           (result = enum_url->Next(1, &stat_url, &fetched)) == S_OK) {
      string16 url_string;
      if (stat_url.pwcsUrl) {
        url_string = stat_url.pwcsUrl;
        CoTaskMemFree(stat_url.pwcsUrl);
      }
      string16 title_string;
      if (stat_url.pwcsTitle) {
        title_string = stat_url.pwcsTitle;
        CoTaskMemFree(stat_url.pwcsTitle);
      }

      GURL url(url_string);
      // Skips the URLs that are invalid or have other schemes.
      if (!url.is_valid() ||
          (std::find(kSchemes, kSchemes + total_schemes, url.scheme()) ==
           kSchemes + total_schemes))
        continue;

      history::URLRow row(url);
      row.set_title(title_string);
      row.set_last_visit(base::Time::FromFileTime(stat_url.ftLastVisited));
      if (stat_url.dwFlags == STATURL_QUERYFLAG_TOPLEVEL) {
        row.set_visit_count(1);
        row.set_hidden(false);
      } else {
        row.set_hidden(true);
      }

      rows.push_back(row);
    }

    if (!rows.empty() && !cancelled()) {
      bridge_->SetHistoryItems(rows, history::SOURCE_IE_IMPORTED);
    }
  }
}

void IEImporter::ImportPasswordsIE6() {
  GUID AutocompleteGUID = kPStoreAutocompleteGUID;
  if (!source_path_.empty()) {
    // We supply a fake GUID for testting.
    AutocompleteGUID = kUnittestGUID;
  }

  // The PStoreCreateInstance function retrieves an interface pointer
  // to a storage provider. But this function has no associated import
  // library or header file, we must call it using the LoadLibrary()
  // and GetProcAddress() functions.
  typedef HRESULT (WINAPI *PStoreCreateFunc)(IPStore**, DWORD, DWORD, DWORD);
  HMODULE pstorec_dll = LoadLibrary(L"pstorec.dll");
  if (!pstorec_dll)
    return;
  PStoreCreateFunc PStoreCreateInstance =
      (PStoreCreateFunc)GetProcAddress(pstorec_dll, "PStoreCreateInstance");
  if (!PStoreCreateInstance) {
    FreeLibrary(pstorec_dll);
    return;
  }

  base::win::ScopedComPtr<IPStore, &IID_IPStore> pstore;
  HRESULT result = PStoreCreateInstance(pstore.Receive(), 0, 0, 0);
  if (result != S_OK) {
    FreeLibrary(pstorec_dll);
    return;
  }

  std::vector<AutoCompleteInfo> ac_list;

  // Enumerates AutoComplete items in the protected database.
  base::win::ScopedComPtr<IEnumPStoreItems, &IID_IEnumPStoreItems> item;
  result = pstore->EnumItems(0, &AutocompleteGUID,
                             &AutocompleteGUID, 0, item.Receive());
  if (result != PST_E_OK) {
    pstore.Release();
    FreeLibrary(pstorec_dll);
    return;
  }

  wchar_t* item_name;
  while (!cancelled() && SUCCEEDED(item->Next(1, &item_name, 0))) {
    DWORD length = 0;
    unsigned char* buffer = NULL;
    result = pstore->ReadItem(0, &AutocompleteGUID, &AutocompleteGUID,
                              item_name, &length, &buffer, NULL, 0);
    if (SUCCEEDED(result)) {
      AutoCompleteInfo ac;
      ac.key = item_name;
      string16 data;
      data.insert(0, reinterpret_cast<wchar_t*>(buffer),
                  length / sizeof(wchar_t));

      // The key name is always ended with ":StringData".
      const wchar_t kDataSuffix[] = L":StringData";
      size_t i = ac.key.rfind(kDataSuffix);
      if (i != string16::npos && ac.key.substr(i) == kDataSuffix) {
        ac.key.erase(i);
        ac.is_url = (ac.key.find(L"://") != string16::npos);
        ac_list.push_back(ac);
        base::SplitString(data, L'\0', &ac_list[ac_list.size() - 1].data);
      }
      CoTaskMemFree(buffer);
    }
    CoTaskMemFree(item_name);
  }
  // Releases them before unload the dll.
  item.Release();
  pstore.Release();
  FreeLibrary(pstorec_dll);

  size_t i;
  for (i = 0; i < ac_list.size(); i++) {
    if (!ac_list[i].is_url || ac_list[i].data.size() < 2)
      continue;

    GURL url(ac_list[i].key.c_str());
    if (!(LowerCaseEqualsASCII(url.scheme(), chrome::kHttpScheme) ||
        LowerCaseEqualsASCII(url.scheme(), chrome::kHttpsScheme))) {
      continue;
    }

    webkit::forms::PasswordForm form;
    GURL::Replacements rp;
    rp.ClearUsername();
    rp.ClearPassword();
    rp.ClearQuery();
    rp.ClearRef();
    form.origin = url.ReplaceComponents(rp);
    form.username_value = ac_list[i].data[0];
    form.password_value = ac_list[i].data[1];
    form.signon_realm = url.GetOrigin().spec();

    // This is not precise, because a scheme of https does not imply a valid
    // certificate was presented; however we assign it this way so that if we
    // import a password from IE whose scheme is https, we give it the benefit
    // of the doubt and DONT auto-fill it unless the form appears under
    // valid SSL conditions.
    form.ssl_valid = url.SchemeIsSecure();

    // Goes through the list to find out the username field
    // of the web page.
    size_t list_it, item_it;
    for (list_it = 0; list_it < ac_list.size(); ++list_it) {
      if (ac_list[list_it].is_url)
        continue;

      for (item_it = 0; item_it < ac_list[list_it].data.size(); ++item_it)
        if (ac_list[list_it].data[item_it] == form.username_value) {
          form.username_element = ac_list[list_it].key;
          break;
        }
    }

    bridge_->SetPasswordForm(form);
  }
}

void IEImporter::ImportPasswordsIE7() {
  if (!source_path_.empty()) {
    // We have been called from the unit tests. Don't import real passwords.
    return;
  }

  base::win::RegKey key(HKEY_CURRENT_USER, kStorage2Path, KEY_READ);
  base::win::RegistryValueIterator reg_iterator(HKEY_CURRENT_USER,
                                                kStorage2Path);
  IE7PasswordInfo password_info;
  while (reg_iterator.Valid() && !cancelled()) {
    // Get the size of the encrypted data.
    DWORD value_len = 0;
    key.ReadValue(reg_iterator.Name(), NULL, &value_len, NULL);
    if (value_len) {
      // Query the encrypted data.
      password_info.encrypted_data.resize(value_len);
      if (key.ReadValue(reg_iterator.Name(),
                        &password_info.encrypted_data.front(),
                        &value_len, NULL) == ERROR_SUCCESS) {
        password_info.url_hash = reg_iterator.Name();
        password_info.date_created = base::Time::Now();

        bridge_->AddIE7PasswordInfo(password_info);
      }
    }

    ++reg_iterator;
  }
}

void IEImporter::ImportSearchEngines() {
  // On IE, search engines are stored in the registry, under:
  // Software\Microsoft\Internet Explorer\SearchScopes
  // Each key represents a search engine. The URL value contains the URL and
  // the DisplayName the name.
  std::map<std::string, TemplateURL*> search_engines_map;
  base::win::RegistryKeyIterator key_iterator(HKEY_CURRENT_USER,
                                              kSearchScopePath);
  while (key_iterator.Valid()) {
    string16 sub_key_name = kSearchScopePath;
    sub_key_name.append(L"\\").append(key_iterator.Name());
    base::win::RegKey sub_key(HKEY_CURRENT_USER, sub_key_name.c_str(),
                              KEY_READ);
    string16 wide_url;
    if ((sub_key.ReadValue(L"URL", &wide_url) != ERROR_SUCCESS) ||
        wide_url.empty()) {
      VLOG(1) << "No URL for IE search engine at " << key_iterator.Name();
      ++key_iterator;
      continue;
    }
    // For the name, we try the default value first (as Live Search uses a
    // non displayable name in DisplayName, and the readable name under the
    // default value).
    string16 name;
    if ((sub_key.ReadValue(NULL, &name) != ERROR_SUCCESS) || name.empty()) {
      // Try the displayable name.
      if ((sub_key.ReadValue(L"DisplayName", &name) != ERROR_SUCCESS) ||
          name.empty()) {
        VLOG(1) << "No name for IE search engine at " << key_iterator.Name();
        ++key_iterator;
        continue;
      }
    }

    std::string url(WideToUTF8(wide_url));
    std::map<std::string, TemplateURL*>::iterator t_iter =
        search_engines_map.find(url);
    TemplateURL* template_url =
        (t_iter != search_engines_map.end()) ? t_iter->second : NULL;
    if (!template_url) {
      // First time we see that URL.
      template_url = new TemplateURL();
      template_url->set_short_name(name);
      template_url->SetURL(url, 0, 0);
      // Give this a keyword to facilitate tab-to-search, if possible.
      GURL gurl = GURL(url);
      template_url->set_keyword(TemplateURLService::GenerateKeyword(gurl,
                                                                    false));
      template_url->set_logo_id(
          TemplateURLPrepopulateData::GetSearchEngineLogo(gurl));
      template_url->set_show_in_default_list(true);
      search_engines_map[url] = template_url;
    }
    ++key_iterator;
  }

  // ProfileWriter::AddKeywords() requires a vector and we have a map.
  std::map<std::string, TemplateURL*>::iterator i;
  std::vector<TemplateURL*> search_engines;
  for (i = search_engines_map.begin(); i != search_engines_map.end(); ++i)
    search_engines.push_back(i->second);

  // Import the list of search engines, but do not override the default.
  bridge_->SetKeywords(search_engines, -1 /*default_keyword_index*/, true);
}

void IEImporter::ImportHomepage() {
  const wchar_t* kIEHomepage = L"Start Page";
  const wchar_t* kIEDefaultHomepage = L"Default_Page_URL";

  base::win::RegKey key(HKEY_CURRENT_USER, kIESettingsMain, KEY_READ);
  string16 homepage_url;
  if (key.ReadValue(kIEHomepage, &homepage_url) != ERROR_SUCCESS ||
      homepage_url.empty())
    return;

  GURL homepage = GURL(homepage_url);
  if (!homepage.is_valid())
    return;

  // Check to see if this is the default website and skip import.
  base::win::RegKey keyDefault(HKEY_LOCAL_MACHINE, kIESettingsMain, KEY_READ);
  string16 default_homepage_url;
  LONG result = keyDefault.ReadValue(kIEDefaultHomepage, &default_homepage_url);
  if (result == ERROR_SUCCESS && !default_homepage_url.empty()) {
    if (homepage.spec() == GURL(default_homepage_url).spec())
      return;
  }

  bridge_->AddHomePage(homepage);
}

string16 IEImporter::ResolveInternetShortcut(const string16& file) {
  base::win::ScopedCoMem<wchar_t> url;
  base::win::ScopedComPtr<IUniformResourceLocator> url_locator;
  HRESULT result = url_locator.CreateInstance(CLSID_InternetShortcut, NULL,
                                              CLSCTX_INPROC_SERVER);
  if (FAILED(result))
    return string16();

  base::win::ScopedComPtr<IPersistFile> persist_file;
  result = persist_file.QueryFrom(url_locator);
  if (FAILED(result))
    return string16();

  // Loads the Internet Shortcut from persistent storage.
  result = persist_file->Load(file.c_str(), STGM_READ);
  if (FAILED(result))
    return string16();

  result = url_locator->GetURL(&url);
  // GetURL can return S_FALSE (FAILED(S_FALSE) is false) when url == NULL.
  if (FAILED(result) || (url == NULL))
    return string16();

  return string16(url);
}

bool IEImporter::GetFavoritesInfo(IEImporter::FavoritesInfo* info) {
  if (!source_path_.empty()) {
    // Source path exists during testing.
    info->path = source_path_;
    info->path = info->path.AppendASCII("Favorites");
    info->links_folder = L"Links";
    return true;
  }

  // IE stores the favorites in the Favorites under user profile's folder.
  wchar_t buffer[MAX_PATH];
  if (FAILED(SHGetFolderPath(NULL, CSIDL_FAVORITES, NULL,
                             SHGFP_TYPE_CURRENT, buffer)))
    return false;
  info->path = FilePath(buffer);

  // There is a Links folder under Favorites folder in Windows Vista, but it
  // is not recording in Vista's registry. So in Vista, we assume the Links
  // folder is under Favorites folder since it looks like there is not name
  // different in every language version of Windows Vista.
  if (base::win::GetVersion() < base::win::VERSION_VISTA) {
    // The Link folder name is stored in the registry.
    DWORD buffer_length = sizeof(buffer);
    base::win::RegKey reg_key(HKEY_CURRENT_USER, kIEToolbarKey, KEY_READ);
    if (reg_key.ReadValue(L"LinksFolderName", buffer,
                          &buffer_length, NULL) != ERROR_SUCCESS)
      return false;
    info->links_folder = buffer;
  } else {
    info->links_folder = L"Links";
  }

  return true;
}

void IEImporter::ParseFavoritesFolder(const FavoritesInfo& info,
                                      BookmarkVector* bookmarks) {
  FilePath file;
  std::vector<FilePath::StringType> file_list;
  FilePath favorites_path(info.path);
  // Favorites path length.  Make sure it doesn't include the trailing \.
  size_t favorites_path_len =
      favorites_path.StripTrailingSeparators().value().size();
  file_util::FileEnumerator file_enumerator(
      favorites_path, true, file_util::FileEnumerator::FILES);
  while (!(file = file_enumerator.Next()).value().empty() && !cancelled())
    file_list.push_back(file.value());

  // Keep the bookmarks in alphabetical order.
  std::sort(file_list.begin(), file_list.end());

  for (std::vector<FilePath::StringType>::iterator it = file_list.begin();
       it != file_list.end(); ++it) {
    FilePath shortcut(*it);
    if (!LowerCaseEqualsASCII(shortcut.Extension(), ".url"))
      continue;

    // Skip the bookmark with invalid URL.
    GURL url = GURL(ResolveInternetShortcut(*it));
    if (!url.is_valid())
      continue;

    // Make the relative path from the Favorites folder, without the basename.
    // ex. Suppose that the Favorites folder is C:\Users\Foo\Favorites.
    //   C:\Users\Foo\Favorites\Foo.url -> ""
    //   C:\Users\Foo\Favorites\Links\Bar\Baz.url -> "Links\Bar"
    FilePath::StringType relative_string =
        shortcut.DirName().value().substr(favorites_path_len);
    if (!relative_string.empty() && FilePath::IsSeparator(relative_string[0]))
      relative_string = relative_string.substr(1);
    FilePath relative_path(relative_string);

    ProfileWriter::BookmarkEntry entry;
    // Remove the dot, the file extension, and the directory path.
    entry.title = shortcut.RemoveExtension().BaseName().value();
    entry.url = url;
    entry.creation_time = GetFileCreationTime(*it);
    if (!relative_path.empty())
      relative_path.GetComponents(&entry.path);

    // Add the bookmark.
    if (!entry.path.empty() && entry.path[0] == info.links_folder) {
      // Bookmarks in the Link folder should be imported to the toolbar.
      entry.in_toolbar = true;
    }
    bookmarks->push_back(entry);
  }

  // Reflect the menu order in IE.
  SortBookmarksInIEOrder(this, bookmarks);
}

int IEImporter::CurrentIEVersion() const {
  static int version = -1;
  if (version < 0) {
    wchar_t buffer[128];
    DWORD buffer_length = sizeof(buffer);
    base::win::RegKey reg_key(HKEY_LOCAL_MACHINE, kIEVersionKey, KEY_READ);
    LONG result = reg_key.ReadValue(L"Version", buffer, &buffer_length, NULL);
    version = ((result == ERROR_SUCCESS)? _wtoi(buffer) : 0);
  }
  return version;
}
