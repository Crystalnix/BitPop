// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search_engines/template_url_prepopulate_data.h"

#if defined(OS_POSIX) && !defined(OS_MACOSX)
#include <locale.h>
#endif

#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/scoped_vector.h"
#include "base/string16.h"
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/stl_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/search_engine_type.h"
#include "chrome/browser/search_engines/search_terms_data.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_WIN)
#undef IN  // On Windows, windef.h defines this, which screws up "India" cases.
#elif defined(OS_MACOSX)
#include "base/mac/scoped_cftyperef.h"
#endif

namespace {

// NOTE: See comments in GetDataVersion() below!  You should probably not change
// the data in this file without changing the result of that function!

// Engine definitions //////////////////////////////////////////////////////////

struct PrepopulatedEngine {
  const wchar_t* const name;
  const wchar_t* const keyword;
  const char* const favicon_url;  // If NULL, there is no favicon.
  const char* const search_url;
  const char* const encoding;
  const char* const suggest_url;  // If NULL, this engine does not support
                                  // suggestions.
  const char* const instant_url;  // If NULL, this engine does not support
                                  // instant.
  // SEARCH_ENGINE_OTHER if there is no matching type.
  const SearchEngineType type;
  // Unique id for this prepopulate engine (corresponds to
  // TemplateURL::prepopulate_id). This ID must be greater than zero and must
  // remain the same for a particular site regardless of how the url changes;
  // the ID is used when modifying engine data in subsequent versions, so that
  // we can find the "old" entry to update even when the name or URL changes.
  //
  // This ID must be "unique" within one country's prepopulated data, but two
  // entries can share an ID if they represent the "same" engine (e.g. Yahoo! US
  // vs. Yahoo! UK) and will not appear in the same user-visible data set.  This
  // facilitates changes like adding more specific per-country data in the
  // future; in such a case the localized engines will transparently replace the
  // previous, non-localized versions.  For engines where we need two instances
  // to appear for one country (e.g. Bing Search U.S. English and Spanish), we
  // must use two different unique IDs (and different keywords).
  //
  // The following unique IDs are available:
  //    53, 56, 58, 60, 61, 64, 65, 66, 70, 74, 78, 79, 80, 81, 84, 86, 88, 91,
  //    92, 93, 94, 95, 96, 97, 98, 99, 102+
  //
  // IDs > 1000 are reserved for distribution custom engines.
  //
  // NOTES:
  //   CHANGE THE ABOVE NUMBERS IF YOU ADD A NEW ENGINE; ID conflicts = bad!
  //   CHANGE kMaxPrepopulatedEngineID below if you add new engine outside
  //       of the current range or it will not be counted in stats.
  const int id;
};

// Prepopulated engines ////////////////////////////////////////////////////////

// The following engines are included in country lists and are added to the list
// of search engines on the first run depending on user's country.

const PrepopulatedEngine abcsok = {
  L"ABC S\x00f8k",
  L"abcsok.no",
  "http://abcsok.no/favicon.ico",
  "http://abcsok.no/index.html?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_ABCSOK,
  72,
};

const PrepopulatedEngine altavista = {
  L"AltaVista",
  L"altavista.com",
  "http://www.altavista.com/favicon.ico",
  "http://www.altavista.com/web/results?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_ALTAVISTA,
  89,
};

const PrepopulatedEngine altavista_ar = {
  L"AltaVista",
  L"ar.altavista.com",
  "http://ar.altavista.com/favicon.ico",
  "http://ar.altavista.com/web/results?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_ALTAVISTA,
  89,
};

const PrepopulatedEngine altavista_se = {
  L"AltaVista",
  L"se.altavista.com",
  "http://se.altavista.com/favicon.ico",
  "http://se.altavista.com/web/results?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_ALTAVISTA,
  89,
};

const PrepopulatedEngine aol = {
  L"AOL",
  L"aol.com",
  "http://search.aol.com/favicon.ico",
  "http://search.aol.com/aol/search?query={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  35,
};

const PrepopulatedEngine araby = {
  L"\x0639\x0631\x0628\x064a",
  L"araby.com",
  "http://araby.com/favicon.ico",
  "http://araby.com/?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  12,
};

const PrepopulatedEngine ask = {
  L"Ask",
  L"ask.com",
  "http://www.ask.com/favicon.ico",
  "http://www.ask.com/web?q={searchTerms}",
  "UTF-8",
  "http://ss.ask.com/query?q={searchTerms}&li=ff",
  NULL,
  SEARCH_ENGINE_ASK,
  4,
};

const PrepopulatedEngine ask_de = {
  L"Ask.com Deutschland",
  L"de.ask.com",
  "http://de.ask.com/favicon.ico",
  "http://de.ask.com/web?q={searchTerms}",
  "UTF-8",
  "http://ss.de.ask.com/query?q={searchTerms}&li=ff",
  NULL,
  SEARCH_ENGINE_ASK,
  4,
};

const PrepopulatedEngine ask_es = {
  L"Ask.com Espa" L"\x00f1" L"a",
  L"es.ask.com",
  "http://es.ask.com/favicon.ico",
  "http://es.ask.com/web?q={searchTerms}",
  "UTF-8",
  "http://ss.es.ask.com/query?q={searchTerms}&li=ff",
  NULL,
  SEARCH_ENGINE_ASK,
  4,
};

const PrepopulatedEngine ask_it = {
  L"Ask.com Italia",
  L"it.ask.com",
  "http://it.ask.com/favicon.ico",
  "http://it.ask.com/web?q={searchTerms}",
  "UTF-8",
  "http://ss.it.ask.com/query?q={searchTerms}&li=ff",
  NULL,
  SEARCH_ENGINE_ASK,
  4,
};

const PrepopulatedEngine ask_nl = {
  L"Ask.com Nederland",
  L"nl.ask.com",
  "http://nl.ask.com/favicon.ico",
  "http://nl.ask.com/web?q={searchTerms}",
  "UTF-8",
  "http://ss.nl.ask.com/query?q={searchTerms}&li=ff",
  NULL,
  SEARCH_ENGINE_ASK,
  4,
};

const PrepopulatedEngine ask_uk = {
  L"Ask Jeeves",
  L"uk.ask.com",
  "http://uk.ask.com/favicon.ico",
  "http://uk.ask.com/web?q={searchTerms}",
  "UTF-8",
  "http://ss.uk.ask.com/query?q={searchTerms}&li=ff",
  NULL,
  SEARCH_ENGINE_ASK,
  4,
};

const PrepopulatedEngine atlas_cz = {
  L"Atlas",
  L"atlas.cz",
  "http://img.atlas.cz/favicon.ico",
  "http://search.atlas.cz/?q={searchTerms}",
  "windows-1250",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  27,
};

const PrepopulatedEngine atlas_sk = {
  L"ATLAS.SK",
  L"atlas.sk",
  "http://www.atlas.sk/images/favicon.ico",
  "http://hladaj.atlas.sk/fulltext/?phrase={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  27,
};

const PrepopulatedEngine baidu = {
  L"\x767e\x5ea6",
  L"baidu.com",
  "http://www.baidu.com/favicon.ico",
  "http://www.baidu.com/s?wd={searchTerms}",
  "GB2312",
  NULL,
  NULL,
  SEARCH_ENGINE_BAIDU,
  21,
};

const PrepopulatedEngine bing = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_ar_XA = {
  L"Bing",
  L"bing.com_",  // bing.com is taken by bing_en_XA.
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=ar-XA&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  7,  // Can't be 3 as this has to appear in the Arabian countries' lists
      // alongside bing_en_XA.
};

const PrepopulatedEngine bing_bg_BG = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=bg-BG&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_cs_CZ = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=cs-CZ&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_da_DK = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=da-DK&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_de_AT = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=de-AT&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_de_CH = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=de-CH&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_de_DE = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=de-DE&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_el_GR = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=el-GR&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_AU = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-AU&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_CA = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-CA&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_GB = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-GB&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_ID = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-ID&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_IE = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-IE&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_IN = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-IN&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_MY = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-MY&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_NZ = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-NZ&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_PH = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-PH&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_SG = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-SG&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_US = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-US&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_XA = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-XA&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_en_ZA = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=en-ZA&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_es_AR = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=es-AR&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_es_CL = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=es-CL&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_es_ES = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=es-ES&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_es_MX = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=es-MX&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_es_XL = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=es-XL&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_et_EE = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=et-EE&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_fi_FI = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=fi-FI&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_fr_BE = {
  L"Bing",
  L"bing.com_",  // bing.com is taken by bing_nl_BE.
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=fr-BE&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  7,
};

const PrepopulatedEngine bing_fr_CA = {
  L"Bing",
  L"bing.com_",  // bing.com is taken by bing_en_CA.
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=fr-CA&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  7,
};

const PrepopulatedEngine bing_fr_CH = {
  L"Bing",
  L"bing.com_",  // bing.com is taken by bing_de_CH.
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=fr-CH&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  7,
};

const PrepopulatedEngine bing_fr_FR = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=fr-FR&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_he_IL = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=he-IL&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_hr_HR = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=hr-HR&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_hu_HU = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=hu-HU&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_it_IT = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=it-IT&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_ja_JP = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=ja-JP&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_ko_KR = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=ko-KR&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_lt_LT = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=lt-LT&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_lv_LV = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=lv-LV&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_nb_NO = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=nb-NO&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_nl_BE = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=nl-BE&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_nl_NL = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=nl-NL&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_pl_PL = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=pl-PL&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_pt_BR = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=pt-BR&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_pt_PT = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=pt-PT&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_ro_RO = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=ro-RO&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_ru_RU = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=ru-RU&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_sl_SI = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=sl-SI&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_sk_SK = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=sk-SK&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_sv_SE = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=sv-SE&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_th_TH = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=th-TH&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_tr_TR = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=tr-TR&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_uk_UA = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=uk-UA&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_zh_CN = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=zh-CN&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_zh_HK = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=zh-HK&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine bing_zh_TW = {
  L"Bing",
  L"bing.com",
  "http://www.bing.com/s/wlflag.ico",
  "http://www.bing.com/search?setmkt=zh-TW&q={searchTerms}",
  "UTF-8",
  "http://api.bing.com/osjson.aspx?query={searchTerms}&language={language}",
  NULL,
  SEARCH_ENGINE_BING,
  3,
};

const PrepopulatedEngine centrum_cz = {
  L"Centrum.cz",
  L"centrum.cz",
  "http://img.centrum.cz/6/vy2/o/favicon.ico",
  "http://search.centrum.cz/index.php?charset={inputEncoding}&q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_CENTRUM,
  26,
};

const PrepopulatedEngine centrum_sk = {
  L"Centrum.sk",
  L"centrum.sk",
  "http://img.centrum.sk/4/favicon.ico",
  "http://search.centrum.sk/index.php?charset={inputEncoding}&q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_CENTRUM,
  26,
};

const PrepopulatedEngine daum = {
  L"Daum",
  L"daum.net",
  "http://search.daum.net/favicon.ico",
  "http://search.daum.net/search?q={searchTerms}",
  "EUC-KR",
  "http://sug.search.daum.net/search_nsuggest?mod=fxjson&q={searchTerms}",
  NULL,
  SEARCH_ENGINE_DAUM,
  68,
};

const PrepopulatedEngine delfi_lt = {
  L"DELFI",
  L"delfi.lt",
  "http://search.delfi.lt/img/favicon.png",
  "http://search.delfi.lt/search.php?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_DELFI,
  45,
};

const PrepopulatedEngine delfi_lv = {
  L"DELFI",
  L"delfi.lv",
  "http://smart.delfi.lv/img/smart_search.png",
  "http://smart.delfi.lv/i.php?enc={inputEncoding}&q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_DELFI,
  45,
};

const PrepopulatedEngine diri = {
  L"diri",
  L"diri.bg",
  "http://i.dir.bg/diri/images/favicon.ico",
  "http://diri.bg/search.php?textfield={searchTerms}",
  "windows-1251",
  NULL,
  NULL,
  SEARCH_ENGINE_DIRI,
  32,
};

const PrepopulatedEngine eniro_fi = {
  L"Eniro",
  L"eniro.fi",
  "http://eniro.fi/favicon.ico",
  "http://eniro.fi/query?search_word={searchTerms}&what=web_local",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  29,
};

const PrepopulatedEngine eniro_se = {
  L"Eniro",
  L"eniro.se",
  "http://eniro.se/favicon.ico",
  "http://eniro.se/query?search_word={searchTerms}&what=web_local",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  29,
};

const PrepopulatedEngine fonecta_02_fi = {
  L"Fonecta 02.fi",
  L"www.fi",
  "http://www.02.fi/img/favicon.ico",
  "http://www.02.fi/haku/{searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  46,
};

const PrepopulatedEngine goo = {
  L"goo",
  L"search.goo.ne.jp",
  "http://goo.ne.jp/favicon.ico",
  "http://search.goo.ne.jp/web.jsp?MT={searchTerms}&IE={inputEncoding}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_GOO,
  23,
};

const PrepopulatedEngine google = {
  L"Google",
  L"google.com",  // This will be dynamically updated by the TemplateURL system.
  "http://www.google.com/favicon.ico",
  "{google:baseURL}search?q={searchTerms}&{google:RLZ}"
      "{google:acceptedSuggestion}{google:originalQueryForSuggestion}"
      "{google:assistedQueryStats}{google:searchFieldtrialParameter}"
      "sourceid=chrome&ie={inputEncoding}",
  "UTF-8",
  "{google:baseSuggestURL}search?{google:searchFieldtrialParameter}"
      "client=chrome&hl={language}&q={searchTerms}",
  "{google:baseURL}webhp?{google:RLZ}sourceid=chrome-instant&"
      "ie={inputEncoding}{google:instantEnabledParameter}{searchTerms}",
  SEARCH_ENGINE_GOOGLE,
  1,
};

const PrepopulatedEngine guruji = {
  L"guruji",
  L"guruji.com",
  "http://guruji.com/favicon.ico",
  "http://guruji.com/search?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  38,
};

const PrepopulatedEngine hispavista = {
  L"hispavista",
  L"hispavista.com",
  "http://buscar.hispavista.com/favicon.ico",
  "http://buscar.hispavista.com/?cadena={searchTerms}",
  "iso-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  18,
};

const PrepopulatedEngine in = {
  L"in.gr",
  L"in.gr",
  "http://www.in.gr/favicon.ico",
  "http://find.in.gr/?qs={searchTerms}",
  "ISO-8859-7",
  NULL,
  NULL,
  SEARCH_ENGINE_IN,
  54,
};

const PrepopulatedEngine jabse = {
  L"Jabse",
  L"jabse.com",
  "http://www.jabse.com/favicon.ico",
  "http://www.jabse.com/searchmachine.php?query={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  19,
};

const PrepopulatedEngine jubii = {
  L"Jubii",
  L"jubii.dk",
  "http://search.jubii.dk/favicon_jubii.ico",
  "http://search.jubii.dk/cgi-bin/pursuit?query={searchTerms}",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  28,
};

const PrepopulatedEngine kvasir = {
  L"Kvasir",
  L"kvasir.no",
  "http://www.kvasir.no/img/favicon.ico",
  "http://www.kvasir.no/nettsok/searchResult.html?searchExpr={searchTerms}",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  73,
};

const PrepopulatedEngine latne = {
  L"LATNE",
  L"latne.lv",
  "http://latne.lv/favicon.ico",
  "http://latne.lv/siets.php?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  71,
};

const PrepopulatedEngine leit = {
  L"leit.is",
  L"leit.is",
  "http://leit.is/leit.ico",
  "http://leit.is/query.aspx?qt={searchTerms}",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  59,
};

const PrepopulatedEngine libero = {
  L"Libero",
  L"libero.it",
  "http://arianna.libero.it/favicon.ico",
  "http://arianna.libero.it/search/abin/integrata.cgi?query={searchTerms}",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  63,
};

const PrepopulatedEngine mail_ru = {
  L"@MAIL.RU",
  L"mail.ru",
  "http://img.go.mail.ru/favicon.ico",
  "http://go.mail.ru/search?q={searchTerms}",
  "windows-1251",
  "http://suggests.go.mail.ru/chrome?q={searchTerms}",
  NULL,
  SEARCH_ENGINE_MAILRU,
  83,
};

const PrepopulatedEngine maktoob = {
  L"\x0645\x0643\x062a\x0648\x0628",
  L"maktoob.com",
  "http://www.maktoob.com/favicon.ico",
  "http://www.maktoob.com/searchResult.php?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  13,
};

const PrepopulatedEngine masrawy = {
  L"\x0645\x0635\x0631\x0627\x0648\x064a",
  L"masrawy.com",
  "http://www.masrawy.com/new/images/masrawy.ico",
  "http://masrawy.com/new/search.aspx?sr={searchTerms}",
  "windows-1256",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  14,
};

const PrepopulatedEngine mynet = {
  L"MYNET",
  L"mynet.com",
  "http://img.mynet.com/mynetfavori.ico",
  "http://arama.mynet.com/search.aspx?q={searchTerms}&pg=q",
  "windows-1254",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  101,
};

const PrepopulatedEngine najdi = {
  L"Najdi.si",
  L"najdi.si",
  "http://www.najdi.si/master/favicon.ico",
  "http://www.najdi.si/search.jsp?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_NAJDI,
  87,
};

const PrepopulatedEngine nate = {
  L"\xb124\xc774\xd2b8\xb2f7\xcef4",
  L"nate.com",
  "http://nate.search.empas.com/favicon.ico",
  "http://nate.search.empas.com/search/all.html?q={searchTerms}",
  "EUC-KR",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  69,
};

const PrepopulatedEngine naver = {
  L"\xb124\xc774\xbc84",
  L"naver.com",
  "http://search.naver.com/favicon.ico",
  "http://search.naver.com/search.naver?ie={inputEncoding}&query={searchTerms}",
  "UTF-8",
  "http://ac.search.naver.com/autocompl?m=s&ie={inputEncoding}&oe=utf-8&"
      "q={searchTerms}",
  NULL,
  SEARCH_ENGINE_NAVER,
  67,
};

const PrepopulatedEngine neti = {
  L"NETI",
  L"neti.ee",
  "http://www.neti.ee/favicon.ico",
  "http://www.neti.ee/cgi-bin/otsing?query={searchTerms}",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_NETI,
  44,
};

const PrepopulatedEngine netsprint = {
  L"NetSprint",
  L"netsprint.pl",
  "http://netsprint.pl/favicon.ico",
  "http://www.netsprint.pl/serwis/search?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_NETSPRINT,
  30,
};

const PrepopulatedEngine nur_kz = {
  L"NUR.KZ",
  L"nur.kz",
  "http://www.nur.kz/favicon_kz.ico",
  "http://search.nur.kz/?encoding={inputEncoding}&query={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  20,
};

const PrepopulatedEngine ok = {
  L"OK.hu",
  L"ok.hu",
  "http://ok.hu/gfx/favicon.ico",
  "http://ok.hu/katalogus?q={searchTerms}",
  "ISO-8859-2",
  NULL,
  NULL,
  SEARCH_ENGINE_OK,
  6,
};

const PrepopulatedEngine onet = {
  L"Onet.pl",
  L"onet.pl",
  "http://szukaj.onet.pl/favicon.ico",
  "http://szukaj.onet.pl/query.html?qt={searchTerms}",
  "ISO-8859-2",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  75,
};

const PrepopulatedEngine pogodak_rs = {
  L"Pogodak!",
  L"pogodak.rs",
  "http://www.pogodak.rs/favicon.ico",
  "http://www.pogodak.rs/search.jsp?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_POGODAK,
  24,
};

const PrepopulatedEngine rambler = {
  L"Rambler",
  L"rambler.ru",
  "http://www.rambler.ru/favicon.ico",
  "http://www.rambler.ru/srch?words={searchTerms}",
  "windows-1251",
  NULL,
  NULL,
  SEARCH_ENGINE_RAMBLER,
  16,
};

const PrepopulatedEngine rediff = {
  L"Rediff",
  L"rediff.com",
  "http://search1.rediff.com/favicon.ico",
  "http://search1.rediff.com/dirsrch/default.asp?MT={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  37,
};

const PrepopulatedEngine rednano = {
  L"Rednano",
  L"rednano.sg",
  "http://rednano.sg/favicon.ico",
  "http://rednano.sg/sfe/lwi.action?querystring={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  41,
};

const PrepopulatedEngine sanook = {
  L"\x0e2a\x0e19\x0e38\x0e01!",
  L"sanook.com",
  "http://search.sanook.com/favicon.ico",
  "http://search.sanook.com/search.php?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_SANOOK,
  100,
};

const PrepopulatedEngine sapo = {
  L"SAPO",
  L"sapo.pt",
  "http://imgs.sapo.pt/images/sapo.ico",
  "http://pesquisa.sapo.pt/?q={searchTerms}",
  "UTF-8",
  "http://pesquisa.sapo.pt/livesapo?q={searchTerms}",
  NULL,
  SEARCH_ENGINE_SAPO,
  77,
};

const PrepopulatedEngine search_de_CH = {
  L"search.ch",
  L"search.ch",
  "http://www.search.ch/favicon.ico",
  "http://www.search.ch/index.de.html?q={searchTerms}",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  51,
};

const PrepopulatedEngine search_fr_CH = {
  L"search.ch",
  L"search.ch_",  // search.ch is taken by search_de_CH.
  "http://www.search.ch/favicon.ico",
  "http://www.search.ch/index.fr.html?q={searchTerms}",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  22,
};

const PrepopulatedEngine seznam = {
  L"Seznam",
  L"seznam.cz",
  "http://1.im.cz/szn/img/favicon.ico",
  "http://search.seznam.cz/?q={searchTerms}",
  "UTF-8",
  "http:///suggest.fulltext.seznam.cz/?dict=fulltext_ff&phrase={searchTerms}&"
      "encoding={inputEncoding}&response_encoding=utf-8",
  NULL,
  SEARCH_ENGINE_SEZNAM,
  25,
};

const PrepopulatedEngine terra_ar = {
  L"Terra Argentina",
  L"terra.com.ar",
  "http://buscar.terra.com.ar/favicon.ico",
  "http://buscar.terra.com.ar/Default.aspx?query={searchTerms}&source=Search",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  90,
};

const PrepopulatedEngine terra_es = {
  L"Terra",
  L"terra.es",
  "http://buscador.terra.es/favicon.ico",
  "http://buscador.terra.es/Default.aspx?query={searchTerms}&source=Search",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  90,
};

const PrepopulatedEngine tut = {
  L"TUT.BY",
  L"tut.by",
  "http://www.tut.by/favicon.ico",
  "http://search.tut.by/?query={searchTerms}",
  "windows-1251",
  NULL,
  NULL,
  SEARCH_ENGINE_TUT,
  17,
};

const PrepopulatedEngine uol = {
  L"UOL Busca",
  L"busca.uol.com.br",
  "http://busca.uol.com.br/favicon.ico",
  "http://busca.uol.com.br/www/index.html?q={searchTerms}",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  82,
};

const PrepopulatedEngine virgilio = {
  L"Virgilio",
  L"virgilio.it",
  "http://ricerca.alice.it/favicon.ico",
  "http://ricerca.alice.it/ricerca?qs={searchTerms}",
  "ISO-8859-1",
  NULL,
  NULL,
  SEARCH_ENGINE_VIRGILIO,
  62,
};

const PrepopulatedEngine walla = {
  L"\x05d5\x05d5\x05d0\x05dc\x05d4!",
  L"walla.co.il",
  "http://www.walla.co.il/favicon.ico",
  "http://search.walla.co.il/?e=hew&q={searchTerms}",
  "windows-1255",
  NULL,
  NULL,
  SEARCH_ENGINE_WALLA,
  55,
};

const PrepopulatedEngine wp = {
  L"Wirtualna Polska",
  L"wp.pl",
  "http://szukaj.wp.pl/favicon.ico",
  "http://szukaj.wp.pl/szukaj.html?szukaj={searchTerms}",
  "ISO-8859-2",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  76,
};

const PrepopulatedEngine yahoo = {
  L"Yahoo!",
  L"yahoo.com",
  "http://search.yahoo.com/favicon.ico",
  "http://search.yahoo.com/search?ei={inputEncoding}&fr=crmas&p={searchTerms}",
  "UTF-8",
  "http://ff.search.yahoo.com/gossip?output=fxjson&command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

// For regional Yahoo variants without region-specific suggestion service,
// suggestion is disabled. For some of them, we might consider
// using a fallback (e.g. de for at/ch, ca or fr for qc, en for nl, no, hk).
const PrepopulatedEngine yahoo_ar = {
  L"Yahoo! Argentina",
  L"ar.yahoo.com",
  "http://ar.search.yahoo.com/favicon.ico",
  "http://ar.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://ar-sayt.ff.search.yahoo.com/gossip-ar-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_at = {
  L"Yahoo! Suche",
  L"at.yahoo.com",
  "http://at.search.yahoo.com/favicon.ico",
  "http://at.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_au = {
  L"Yahoo!7",
  L"au.yahoo.com",
  "http://au.search.yahoo.com/favicon.ico",
  "http://au.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://aue-sayt.ff.search.yahoo.com/gossip-au-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_br = {
  L"Yahoo! Brasil",
  L"br.yahoo.com",
  "http://br.search.yahoo.com/favicon.ico",
  "http://br.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://br-sayt.ff.search.yahoo.com/gossip-br-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_ca = {
  L"Yahoo! Canada",
  L"ca.yahoo.com",
  "http://ca.search.yahoo.com/favicon.ico",
  "http://ca.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://gossip.ca.yahoo.com/gossip-ca-sayt?output=fxjsonp&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_ch = {
  L"Yahoo! Suche",
  L"ch.yahoo.com",
  "http://ch.search.yahoo.com/favicon.ico",
  "http://ch.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_cl = {
  L"Yahoo! Chile",
  L"cl.yahoo.com",
  "http://cl.search.yahoo.com/favicon.ico",
  "http://cl.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://gossip.telemundo.yahoo.com/gossip-e1-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_cn = {
  L"\x4e2d\x56fd\x96c5\x864e",
  L"cn.yahoo.com",
  "http://search.cn.yahoo.com/favicon.ico",
  "http://search.cn.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "GB2312",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_co = {
  L"Yahoo! Colombia",
  L"co.yahoo.com",
  "http://co.search.yahoo.com/favicon.ico",
  "http://co.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://gossip.telemundo.yahoo.com/gossip-e1-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_de = {
  L"Yahoo! Deutschland",
  L"de.yahoo.com",
  "http://de.search.yahoo.com/favicon.ico",
  "http://de.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://de-sayt.ff.search.yahoo.com/gossip-de-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_dk = {
  L"Yahoo! Danmark",
  L"dk.yahoo.com",
  "http://dk.search.yahoo.com/favicon.ico",
  "http://dk.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_es = {
  L"Yahoo! Espa" L"\x00f1" L"a",
  L"es.yahoo.com",
  "http://es.search.yahoo.com/favicon.ico",
  "http://es.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://es-sayt.ff.search.yahoo.com/gossip-es-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_fi = {
  L"Yahoo!-haku",
  L"fi.yahoo.com",
  "http://fi.search.yahoo.com/favicon.ico",
  "http://fi.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_fr = {
  L"Yahoo! France",
  L"fr.yahoo.com",
  "http://fr.search.yahoo.com/favicon.ico",
  "http://fr.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://fr-sayt.ff.search.yahoo.com/gossip-fr-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_hk = {
  L"Yahoo! Hong Kong",
  L"hk.yahoo.com",
  "http://hk.search.yahoo.com/favicon.ico",
  "http://hk.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_id = {
  L"Yahoo! Indonesia",
  L"id.yahoo.com",
  "http://id.search.yahoo.com/favicon.ico",
  "http://id.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://id-sayt.ff.search.yahoo.com/gossip-id-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_in = {
  L"Yahoo! India",
  L"in.yahoo.com",
  "http://in.search.yahoo.com/favicon.ico",
  "http://in.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://in-sayt.ff.search.yahoo.com/gossip-in-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_it = {
  L"Yahoo! Italia",
  L"it.yahoo.com",
  "http://it.search.yahoo.com/favicon.ico",
  "http://it.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://it-sayt.ff.search.yahoo.com/gossip-it-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_jp = {
  L"Yahoo! JAPAN",
  L"yahoo.co.jp",
  "http://search.yahoo.co.jp/favicon.ico",
  "http://search.yahoo.co.jp/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOOJP,
  2,
};

const PrepopulatedEngine yahoo_kr = {
  L"\xc57c\xd6c4! \xcf54\xb9ac\xc544",
  L"kr.yahoo.com",
  "http://kr.search.yahoo.com/favicon.ico",
  "http://kr.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://kr.atc.search.yahoo.com/atcx.php?property=main&ot=fxjson&"
     "ei=utf8&eo=utf8&command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_malaysia = {
  L"Yahoo! Malaysia",
  L"malaysia.yahoo.com",
  "http://malaysia.search.yahoo.com/favicon.ico",
  "http://malaysia.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://my-sayt.ff.search.yahoo.com/gossip-my-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_mx = {
  L"Yahoo! M\x00e9xico",
  L"mx.yahoo.com",
  "http://mx.search.yahoo.com/favicon.ico",
  "http://mx.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://gossip.mx.yahoo.com/gossip-mx-sayt?output=fxjsonp&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_nl = {
  L"Yahoo! Nederland",
  L"nl.yahoo.com",
  "http://nl.search.yahoo.com/favicon.ico",
  "http://nl.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_no = {
  L"Yahoo! Norge",
  L"no.yahoo.com",
  "http://no.search.yahoo.com/favicon.ico",
  "http://no.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_nz = {
  L"Yahoo!Xtra",
  L"nz.yahoo.com",
  "http://nz.search.yahoo.com/favicon.ico",
  "http://nz.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://aue-sayt.ff.search.yahoo.com/gossip-nz-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_pe = {
  L"Yahoo! Per\x00fa",
  L"pe.yahoo.com",
  "http://pe.search.yahoo.com/favicon.ico",
  "http://pe.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://gossip.telemundo.yahoo.com/gossip-e1-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_ph = {
  L"Yahoo! Philippines",
  L"ph.yahoo.com",
  "http://ph.search.yahoo.com/favicon.ico",
  "http://ph.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://ph-sayt.ff.search.yahoo.com/gossip-ph-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_qc = {
  L"Yahoo! Qu" L"\x00e9" L"bec",
  L"qc.yahoo.com",
  "http://qc.search.yahoo.com/favicon.ico",
  "http://qc.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOOQC,
  5,  // Can't be 2 as this has to appear in the Canada list alongside yahoo_ca.
};

const PrepopulatedEngine yahoo_ru = {
  L"Yahoo! \x043f\x043e-\x0440\x0443\x0441\x0441\x043a\x0438",
  L"ru.yahoo.com",
  "http://ru.search.yahoo.com/favicon.ico",
  "http://ru.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_se = {
  L"Yahoo! Sverige",
  L"se.yahoo.com",
  "http://se.search.yahoo.com/favicon.ico",
  "http://se.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_sg = {
  L"Yahoo! Singapore",
  L"sg.yahoo.com",
  "http://sg.search.yahoo.com/favicon.ico",
  "http://sg.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://sg-sayt.ff.search.yahoo.com/gossip-sg-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_th = {
  L"Yahoo! \x0e1b\x0e23\x0e30\x0e40\x0e17\x0e28\x0e44\x0e17\x0e22",
  L"th.yahoo.com",
  "http://th.search.yahoo.com/favicon.ico",
  "http://th.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://th-sayt.ff.search.yahoo.com/gossip-th-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_tw = {
  L"Yahoo!\x5947\x6469",
  L"tw.yahoo.com",
  "http://tw.search.yahoo.com/favicon.ico",
  "http://tw.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_uk = {
  L"Yahoo! UK & Ireland",
  L"uk.yahoo.com",
  "http://uk.search.yahoo.com/favicon.ico",
  "http://uk.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://uk-sayt.ff.search.yahoo.com/gossip-uk-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_ve = {
  L"Yahoo! Venezuela",
  L"ve.yahoo.com",
  "http://ve.search.yahoo.com/favicon.ico",
  "http://ve.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://gossip.telemundo.yahoo.com/gossip-e1-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yahoo_vn = {
  L"Yahoo! Vi\x1ec7t Nam",
  L"vn.yahoo.com",
  "http://vn.search.yahoo.com/favicon.ico",
  "http://vn.search.yahoo.com/search?ei={inputEncoding}&fr=crmas&"
      "p={searchTerms}",
  "UTF-8",
  "http://vn-sayt.ff.search.yahoo.com/gossip-vn-sayt?output=fxjson&"
      "command={searchTerms}",
  NULL,
  SEARCH_ENGINE_YAHOO,
  2,
};

const PrepopulatedEngine yamli = {
  L"Yamli",
  L"yamli.com",
  "http://www.yamli.com/favicon.ico",
  "http://www.yamli.com/#q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_OTHER,
  11,
};

const PrepopulatedEngine yandex_ru = {
  L"\x042f\x043d\x0434\x0435\x043a\x0441",
  L"yandex.ru",
  "http://yandex.ru/favicon.ico",
  "http://yandex.ru/yandsearch?text={searchTerms}",
  "UTF-8",
  "http://suggest.yandex.net/suggest-ff.cgi?part={searchTerms}",
  NULL,
  SEARCH_ENGINE_YANDEX,
  15,
};

const PrepopulatedEngine yandex_ua = {
  L"\x042f\x043d\x0434\x0435\x043a\x0441",
  L"yandex.ua",
  "http://yandex.ua/favicon.ico",
  "http://yandex.ua/yandsearch?text={searchTerms}",
  "UTF-8",
  "http://suggest.yandex.net/suggest-ff.cgi?part={searchTerms}",
  NULL,
  SEARCH_ENGINE_YANDEX,
  15,
};

const PrepopulatedEngine zoznam = {
  L"Zoznam",
  L"zoznam.sk",
  "http://zoznam.sk/favicon.ico",
  "http://zoznam.sk/hladaj.fcgi?s={searchTerms}",
  "windows-1250",
  NULL,
  NULL,
  SEARCH_ENGINE_ZOZNAM,
  85,
};

// UMA-only engines ////////////////////////////////////////////////////////////

// The following engines are not included in any of the country lists. They
// are listed in |kAllEngines|, however, so that GetEngineType can find them
// for UMA reporting purposes.

const PrepopulatedEngine all_by = {
  L"ALL.BY",
  L"all.by",
  NULL,
  "http://www.all.by/cgi-bin/search.cgi?mode=by&query={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_ALL_BY,
  33,
};

const PrepopulatedEngine aport = {
  L"Aport",
  L"aport.ru",
  NULL,
  "http://www.aport.ru/search/?r={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_APORT,
  34,
};

const PrepopulatedEngine avg = {
  L"AVG Secure Search",
  L"search.avg.com",
  NULL,
  "http://search.avg.com/route/?q={searchTerms}&lng={language}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_AVG,
  50,
};

const PrepopulatedEngine avg_i = {
  L"AVG Secure Search",
  L"isearch.avg.com",
  NULL,
  "http://isearch.avg.com/search?q={searchTerms}&lng={language}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_AVG,
  52,
};

const PrepopulatedEngine conduit = {
  L"Conduit",
  L"conduit.com",
  NULL,
  "http://search.conduit.com/Results.aspx?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_CONDUIT,
  36,
};

const PrepopulatedEngine icq = {
  L"ICQ",
  L"icq.com",
  NULL,
  "http://search.icq.com/search/results.php?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_ICQ,
  39,
};

const PrepopulatedEngine meta_ua = {
  L"Meta-Ukraine",
  L"meta.ua",
  NULL,
  "http://meta.ua/search.asp?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_META_UA,
  40,
};

const PrepopulatedEngine metabot_ru = {
  L"Metabot",
  L"metabot.ru",
  NULL,
  "http://results.metabot.ru/?st={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_METABOT_RU,
  42,
};

const PrepopulatedEngine nigma = {
  L"Nigma",
  L"nigma.ru",
  NULL,
  "http://www.nigma.ru/?s={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_NIGMA,
  43,
};

const PrepopulatedEngine qip = {
  L"QIP",
  L"qip.ru",
  NULL,
  "http://search.qip.ru/?query={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_QIP,
  47,
};

const PrepopulatedEngine ukr_net = {
  L"Ukr.net",
  L"ukr.net",
  NULL,
  "http://search.ukr.net/google/search.php?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_UKR_NET,
  48,
};

const PrepopulatedEngine webalta = {
  L"Webalta",
  L"webalta.ru",
  NULL,
  "http://webalta.ru/search?q={searchTerms}",
  "UTF-8",
  NULL,
  NULL,
  SEARCH_ENGINE_WEBALTA,
  49,
};

const PrepopulatedEngine yandex_tr = {
  L"Yandex",
  L"yandex.com.tr",
  "http://yandex.com.tr/favicon.ico",
  "http://yandex.com.tr/yandsearch?text={searchTerms}",
  "UTF-8",
  "http://suggest.yandex.net/suggest-ff.cgi?part={searchTerms}",
  NULL,
  SEARCH_ENGINE_YANDEX,
  15,
};

// Lists of engines per country ////////////////////////////////////////////////

// Put these in order with most interesting/important first.  The default will
// be the first engine.

// Default (for countries with no better engine set)
const PrepopulatedEngine* engines_default[] =
    { &google, &yahoo, &bing, };

// United Arab Emirates
const PrepopulatedEngine* engines_AE[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, &araby, &maktoob, };

// Albania
const PrepopulatedEngine* engines_AL[] =
    { &google, &yahoo, &bing_en_XA, };

// Argentina
const PrepopulatedEngine* engines_AR[] =
    { &google, &yahoo_ar, &bing_es_AR, &terra_ar, &altavista_ar, };

// Austria
const PrepopulatedEngine* engines_AT[] =
    { &google, &yahoo_at, &bing_de_AT, };

// Australia
const PrepopulatedEngine* engines_AU[] =
    { &google, &bing_en_AU, &yahoo_au, };

// Bosnia and Herzegovina
const PrepopulatedEngine* engines_BA[] =
    { &google, &yahoo, &bing, };

// Belgium
const PrepopulatedEngine* engines_BE[] =
    { &google, &bing_nl_BE, &yahoo, &bing_fr_BE, };

// Bulgaria
const PrepopulatedEngine* engines_BG[] =
    { &google, &bing_bg_BG, &yahoo, &diri, &jabse, };

// Bahrain
const PrepopulatedEngine* engines_BH[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, };

// Burundi
const PrepopulatedEngine* engines_BI[] =
    { &google, &yahoo, &bing, };

// Brunei
const PrepopulatedEngine* engines_BN[] =
    { &google, &yahoo_malaysia, &bing_en_MY, };

// Bolivia
const PrepopulatedEngine* engines_BO[] =
    { &google, &altavista, &bing_es_XL, &yahoo, };

// Brazil
const PrepopulatedEngine* engines_BR[] =
    { &google, &bing_pt_BR, &yahoo_br, &uol, };

// Belarus
const PrepopulatedEngine* engines_BY[] =
    { &google, &tut, &yandex_ru, &rambler, &yahoo, };

// Belize
const PrepopulatedEngine* engines_BZ[] =
    { &google, &yahoo, &bing, &aol, };

// Canada
const PrepopulatedEngine* engines_CA[] =
    { &google, &yahoo_ca, &yahoo_qc, &bing_en_CA, &bing_fr_CA, };

// Switzerland
const PrepopulatedEngine* engines_CH[] =
    { &google, &yahoo_ch, &bing_de_CH, &bing_fr_CH, &search_de_CH,
      &search_fr_CH, };

// Chile
const PrepopulatedEngine* engines_CL[] =
    { &google, &yahoo_cl, &bing_es_CL, };

// China
const PrepopulatedEngine* engines_CN[] =
    { &google, &baidu, &yahoo_cn, &bing_zh_CN, };

// Colombia
const PrepopulatedEngine* engines_CO[] =
    { &google, &bing_es_XL, &yahoo_co, };

// Costa Rica
const PrepopulatedEngine* engines_CR[] =
    { &google, &bing_es_XL, &yahoo, };

// Czech Republic
const PrepopulatedEngine* engines_CZ[] =
    { &google, &seznam, &bing_cs_CZ, &centrum_cz, &atlas_cz, };

// Germany
const PrepopulatedEngine* engines_DE[] =
    { &google, &ask_de, &bing_de_DE, &yahoo_de };

// Denmark
const PrepopulatedEngine* engines_DK[] =
    { &google, &bing_da_DK, &yahoo_dk, };

// Dominican Republic
const PrepopulatedEngine* engines_DO[] =
    { &google, &bing_es_XL, &yahoo, };

// Algeria
const PrepopulatedEngine* engines_DZ[] =
    { &google, &bing_en_XA, &yahoo, &bing_ar_XA, &maktoob, };

// Ecuador
const PrepopulatedEngine* engines_EC[] =
    { &google, &bing_es_XL, &yahoo, };

// Estonia
const PrepopulatedEngine* engines_EE[] =
    { &google, &bing_et_EE, &neti, &yahoo, };

// Egypt
const PrepopulatedEngine* engines_EG[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, &masrawy, };

// Spain
const PrepopulatedEngine* engines_ES[] =
    { &google, &ask_es, &bing_es_ES, &yahoo_es, &terra_es, &hispavista, };

// Faroe Islands
const PrepopulatedEngine* engines_FO[] =
    { &google, &bing_da_DK, &yahoo_dk, &jubii };

// Finland
const PrepopulatedEngine* engines_FI[] =
    { &google, &bing_fi_FI, &yahoo_fi, &eniro_fi, &fonecta_02_fi, };

// France
const PrepopulatedEngine* engines_FR[] =
    { &google, &yahoo_fr, &bing_fr_FR, };

// United Kingdom
const PrepopulatedEngine* engines_GB[] =
    { &google, &ask_uk, &yahoo_uk, &bing_en_GB, };

// Greece
const PrepopulatedEngine* engines_GR[] =
    { &google, &yahoo, &in, &bing_el_GR };

// Guatemala
const PrepopulatedEngine* engines_GT[] =
    { &google, &bing_es_XL, &yahoo, &ask_es, };

// Hong Kong
const PrepopulatedEngine* engines_HK[] =
    { &google, &yahoo_hk, &bing_zh_HK, &baidu, };

// Honduras
const PrepopulatedEngine* engines_HN[] =
    { &google, &bing_es_XL, &yahoo, &ask_es, };

// Croatia
const PrepopulatedEngine* engines_HR[] =
    { &google, &yahoo, &bing_hr_HR, };

// Hungary
const PrepopulatedEngine* engines_HU[] =
    { &google, &ok, &bing_hu_HU, };

// Indonesia
const PrepopulatedEngine* engines_ID[] =
    { &google, &yahoo_id, &bing_en_ID, };

// Ireland
const PrepopulatedEngine* engines_IE[] =
    { &google, &yahoo_uk, &bing_en_IE, };

// Israel
const PrepopulatedEngine* engines_IL[] =
    { &google, &walla, &bing_he_IL, };

// India
const PrepopulatedEngine* engines_IN[] =
    { &google, &yahoo_in, &bing_en_IN, &rediff, &guruji, };

// Iraq
const PrepopulatedEngine* engines_IQ[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, &maktoob, &ask, };

// Iran
const PrepopulatedEngine* engines_IR[] =
    { &google, &yahoo, };

// Iceland
const PrepopulatedEngine* engines_IS[] =
    { &google, &yahoo, &bing, &leit, };

// Italy
const PrepopulatedEngine* engines_IT[] =
    { &google, &ask_it, &virgilio, &bing_it_IT, &yahoo_it, &libero, };

// Jamaica
const PrepopulatedEngine* engines_JM[] =
    { &google, &yahoo, &bing, };

// Jordan
const PrepopulatedEngine* engines_JO[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, &maktoob, &araby, };

// Japan
const PrepopulatedEngine* engines_JP[] =
    { &google, &yahoo_jp, &bing_ja_JP, &goo, };

// Kenya
const PrepopulatedEngine* engines_KE[] =
    { &google, &yahoo, &bing, };

// Kuwait
const PrepopulatedEngine* engines_KW[] =
    { &google, &bing_en_XA, &yahoo, &bing_ar_XA, &maktoob, &araby, };

// South Korea
const PrepopulatedEngine* engines_KR[] =
    { &google, &naver, &daum, &yahoo_kr, &nate, };

// Kazakhstan
const PrepopulatedEngine* engines_KZ[] =
    { &google, &rambler, &yandex_ru, &nur_kz, };

// Lebanon
const PrepopulatedEngine* engines_LB[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, &maktoob, &araby, };

// Liechtenstein
const PrepopulatedEngine* engines_LI[] =
    { &google, &bing_de_DE, &yahoo_de, };

// Lithuania
const PrepopulatedEngine* engines_LT[] =
    { &google, &delfi_lt, &yahoo, &bing_lt_LT, };

// Luxembourg
const PrepopulatedEngine* engines_LU[] =
    { &google, &bing_fr_FR, &yahoo_fr, };

// Latvia
const PrepopulatedEngine* engines_LV[] =
    { &google, &bing, &yandex_ru, &yahoo, &latne, };

// Libya
const PrepopulatedEngine* engines_LY[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, &maktoob, &ask, };

// Morocco
const PrepopulatedEngine* engines_MA[] =
    { &google, &bing_en_XA, &yahoo, &bing_ar_XA, };

// Monaco
const PrepopulatedEngine* engines_MC[] =
    { &google, &bing_fr_FR, &yahoo_fr, };

// Moldova
const PrepopulatedEngine* engines_MD[] =
    { &google, &yandex_ru, &yahoo, &bing, };

// Montenegro
const PrepopulatedEngine* engines_ME[] =
    { &google, &yahoo, &bing };

// Macedonia
const PrepopulatedEngine* engines_MK[] =
    { &google, &yahoo, &bing, };

// Mexico
const PrepopulatedEngine* engines_MX[] =
    { &google, &bing_es_MX, &yahoo_mx, };

// Malaysia
const PrepopulatedEngine* engines_MY[] =
    { &google, &yahoo_malaysia, &bing_en_MY, };

// Nicaragua
const PrepopulatedEngine* engines_NI[] =
    { &google, &bing_es_XL, &yahoo, &ask_es, };

// Netherlands
const PrepopulatedEngine* engines_NL[] =
    { &google, &bing_nl_NL, &yahoo_nl, &ask_nl, };

// Norway
const PrepopulatedEngine* engines_NO[] =
    { &google, &bing_nb_NO, &abcsok, &yahoo_no, &kvasir, };

// New Zealand
const PrepopulatedEngine* engines_NZ[] =
    { &google, &yahoo_nz, &bing_en_NZ, };

// Oman
const PrepopulatedEngine* engines_OM[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, };

// Panama
const PrepopulatedEngine* engines_PA[] =
    { &google, &bing_es_XL, &yahoo, &ask_es, };

// Peru
const PrepopulatedEngine* engines_PE[] =
    { &google, &bing_es_XL, &yahoo_pe, };

// Philippines
const PrepopulatedEngine* engines_PH[] =
    { &google, &yahoo_ph, &bing_en_PH, };

// Pakistan
const PrepopulatedEngine* engines_PK[] =
    { &google, &yahoo, &bing, };

// Puerto Rico
const PrepopulatedEngine* engines_PR[] =
    { &google, &bing_es_XL, &yahoo, &ask_es, };

// Poland
const PrepopulatedEngine* engines_PL[] =
    { &google, &bing_pl_PL, &netsprint, &yahoo_uk, &onet, &wp,  };

// Portugal
const PrepopulatedEngine* engines_PT[] =
    { &google, &sapo, &bing_pt_PT, &yahoo, };

// Paraguay
const PrepopulatedEngine* engines_PY[] =
    { &google, &bing_es_XL, &yahoo, };

// Qatar
const PrepopulatedEngine* engines_QA[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, &maktoob, &araby };

// Romania
const PrepopulatedEngine* engines_RO[] =
    { &google, &yahoo_uk, &bing_ro_RO, };

// Serbia
const PrepopulatedEngine* engines_RS[] =
    { &google, &pogodak_rs, &bing, };

// Russia
const PrepopulatedEngine* engines_RU[] =
    { &google, &yandex_ru, &mail_ru, &tut, &rambler, &bing_ru_RU, };

// Rwanda
const PrepopulatedEngine* engines_RW[] =
    { &google, &yahoo, &bing, };

// Saudi Arabia
const PrepopulatedEngine* engines_SA[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, };

// Sweden
const PrepopulatedEngine* engines_SE[] =
    { &google, &bing_sv_SE, &yahoo_se, &altavista_se, &eniro_se };

// Singapore
const PrepopulatedEngine* engines_SG[] =
    { &google, &yahoo_sg, &bing_en_SG, &rednano, };

// Slovenia
const PrepopulatedEngine* engines_SI[] =
    { &google, &najdi, &yahoo, &bing_sl_SI, };

// Slovakia
const PrepopulatedEngine* engines_SK[] =
    { &google, &zoznam, &bing_sk_SK, &atlas_sk, &centrum_sk };

// El Salvador
const PrepopulatedEngine* engines_SV[] =
    { &google, &bing_es_XL, &yahoo, };

// Syria
const PrepopulatedEngine* engines_SY[] =
    { &google, &bing_en_XA, &yahoo, &bing_ar_XA, &maktoob, &yamli, };

// Thailand
const PrepopulatedEngine* engines_TH[] =
    { &google, &sanook, &yahoo_th, &bing_th_TH, };

// Tunisia
const PrepopulatedEngine* engines_TN[] =
    { &google, &bing_en_XA, &yahoo, &bing_ar_XA, &maktoob, &yamli };

// Turkey
const PrepopulatedEngine* engines_TR[] =
    { &google, &bing_tr_TR, &yahoo, &mynet, };

// Trinidad and Tobago
const PrepopulatedEngine* engines_TT[] =
    { &google, &bing, &yahoo, &aol, };

// Taiwan
const PrepopulatedEngine* engines_TW[] =
    { &google, &yahoo_tw, &bing_zh_TW, };

// Tanzania
const PrepopulatedEngine* engines_TZ[] =
    { &google, &yahoo, &bing, };

// Ukraine
const PrepopulatedEngine* engines_UA[] =
    { &google, &yandex_ua, &mail_ru, &rambler, };

// United States
const PrepopulatedEngine* engines_US[] =
    { &google, &yahoo, &bing_en_US, };

// Uruguay
const PrepopulatedEngine* engines_UY[] =
    { &google, &bing_es_XL, &yahoo, };

// Venezuela
const PrepopulatedEngine* engines_VE[] =
    { &google, &bing_es_XL, &yahoo_ve, };

// Vietnam
const PrepopulatedEngine* engines_VN[] =
    { &google, &yahoo_vn, };

// Yemen
const PrepopulatedEngine* engines_YE[] =
    { &google, &yahoo, &bing_en_XA, &bing_ar_XA, &maktoob, &araby, };

// South Africa
const PrepopulatedEngine* engines_ZA[] =
    { &google, &yahoo, &bing_en_ZA, };

// Zimbabwe
const PrepopulatedEngine* engines_ZW[] =
    { &google, &yahoo, &bing, };


// A list of all the engines that we know about.
const PrepopulatedEngine* kAllEngines[] =
    { // Prepopulated engines:
      &abcsok, &altavista, &altavista_ar, &altavista_se, &aol, &araby, &ask,
      &ask_de, &ask_es, &ask_it, &ask_nl, &ask_uk, &atlas_cz, &atlas_sk, &baidu,
      &bing, &bing_ar_XA, &bing_bg_BG, &bing_cs_CZ, &bing_da_DK, &bing_de_AT,
      &bing_de_CH, &bing_de_DE, &bing_el_GR, &bing_en_AU, &bing_en_CA,
      &bing_en_GB, &bing_en_ID, &bing_en_IE, &bing_en_IN, &bing_en_MY,
      &bing_en_NZ, &bing_en_PH, &bing_en_SG, &bing_en_US, &bing_en_XA,
      &bing_en_ZA, &bing_es_AR, &bing_es_CL, &bing_es_ES, &bing_es_MX,
      &bing_es_XL, &bing_et_EE, &bing_fi_FI, &bing_fr_BE, &bing_fr_CA,
      &bing_fr_CH, &bing_fr_FR, &bing_he_IL, &bing_hr_HR, &bing_hu_HU,
      &bing_it_IT, &bing_ja_JP, &bing_ko_KR, &bing_lt_LT, &bing_lv_LV,
      &bing_nb_NO, &bing_nl_BE, &bing_nl_NL, &bing_pl_PL, &bing_pt_BR,
      &bing_pt_PT, &bing_ro_RO, &bing_ru_RU, &bing_sl_SI, &bing_sk_SK,
      &bing_sv_SE, &bing_th_TH, &bing_tr_TR, &bing_uk_UA, &bing_zh_CN,
      &bing_zh_HK, &bing_zh_TW, &centrum_cz, &centrum_sk, &daum, &delfi_lt,
      &delfi_lv, &diri, &eniro_fi, &eniro_se, &fonecta_02_fi, &goo, &google,
      &guruji, &hispavista, &in, &jabse, &jubii, &kvasir, &latne, &leit,
      &libero, &mail_ru, &maktoob, &masrawy, &mynet, &najdi, &nate, &naver,
      &neti, &netsprint, &nur_kz, &ok, &onet, &pogodak_rs, &rambler, &rediff,
      &rednano, &sanook, &sapo, &search_de_CH, &search_fr_CH, &seznam,
      &terra_ar, &terra_es, &tut, &uol, &virgilio, &walla, &wp, &yahoo,
      &yahoo_ar, &yahoo_at, &yahoo_au, &yahoo_br, &yahoo_ca, &yahoo_ch,
      &yahoo_cl, &yahoo_cn, &yahoo_co, &yahoo_de, &yahoo_dk, &yahoo_es,
      &yahoo_fi, &yahoo_fr, &yahoo_hk, &yahoo_id, &yahoo_in, &yahoo_it,
      &yahoo_jp, &yahoo_kr, &yahoo_malaysia, &yahoo_mx, &yahoo_nl, &yahoo_no,
      &yahoo_nz, &yahoo_pe, &yahoo_ph, &yahoo_qc, &yahoo_ru, &yahoo_se,
      &yahoo_sg, &yahoo_th, &yahoo_tw, &yahoo_uk, &yahoo_ve, &yahoo_vn, &yamli,
      &yandex_ru, &yandex_ua, &zoznam,
      // UMA-only engines:
      &all_by, &aport, &avg, &avg_i, &conduit, &icq, &meta_ua, &metabot_ru,
      &nigma, &qip, &ukr_net, &webalta, &yandex_tr };


// Geographic mappings /////////////////////////////////////////////////////////

// Please refer to ISO 3166-1 for information about the two-character country
// codes; http://en.wikipedia.org/wiki/ISO_3166-1_alpha-2 is useful. In the
// following (C++) code, we pack the two letters of the country code into an int
// value we call the CountryID.

const int kCountryIDUnknown = -1;
const int kCountryIDNotSet = 0;

inline int CountryCharsToCountryID(char c1, char c2) {
  return c1 << 8 | c2;
}

int CountryCharsToCountryIDWithUpdate(char c1, char c2) {
  // SPECIAL CASE: In 2003, Yugoslavia renamed itself to Serbia and Montenegro.
  // Serbia and Montenegro dissolved their union in June 2006. Yugoslavia was
  // ISO 'YU' and Serbia and Montenegro were ISO 'CS'. Serbia was subsequently
  // issued 'RS' and Montenegro 'ME'. Windows XP and Mac OS X Leopard still use
  // the value 'YU'. If we get a value of 'YU' or 'CS' we will map it to 'RS'.
  if ((c1 == 'Y' && c2 == 'U') ||
      (c1 == 'C' && c2 == 'S')) {
    c1 = 'R';
    c2 = 'S';
  }

  // SPECIAL CASE: Timor-Leste changed from 'TP' to 'TL' in 2002. Windows XP
  // predates this; we therefore map this value.
  if (c1 == 'T' && c2 == 'P')
    c2 = 'L';

  return CountryCharsToCountryID(c1, c2);
}

#if defined(OS_WIN)

// For reference, a list of GeoIDs can be found at
// http://msdn.microsoft.com/en-us/library/dd374073.aspx .
int GeoIDToCountryID(GEOID geo_id) {
  const int kISOBufferSize = 3;  // Two plus one for the terminator.
  wchar_t isobuf[kISOBufferSize] = { 0 };
  int retval = GetGeoInfo(geo_id, GEO_ISO2, isobuf, kISOBufferSize, 0);

  if (retval == kISOBufferSize &&
      !(isobuf[0] == L'X' && isobuf[1] == L'X'))
    return CountryCharsToCountryIDWithUpdate(static_cast<char>(isobuf[0]),
                                             static_cast<char>(isobuf[1]));

  // Various locations have ISO codes that Windows does not return.
  switch (geo_id) {
    case 0x144:   // Guernsey
      return CountryCharsToCountryID('G', 'G');
    case 0x148:   // Jersey
      return CountryCharsToCountryID('J', 'E');
    case 0x3B16:  // Isle of Man
      return CountryCharsToCountryID('I', 'M');

    // 'UM' (U.S. Minor Outlying Islands)
    case 0x7F:    // Johnston Atoll
    case 0x102:   // Wake Island
    case 0x131:   // Baker Island
    case 0x146:   // Howland Island
    case 0x147:   // Jarvis Island
    case 0x149:   // Kingman Reef
    case 0x152:   // Palmyra Atoll
    case 0x52FA:  // Midway Islands
      return CountryCharsToCountryID('U', 'M');

    // 'SH' (Saint Helena)
    case 0x12F:  // Ascension Island
    case 0x15C:  // Tristan da Cunha
      return CountryCharsToCountryID('S', 'H');

    // 'IO' (British Indian Ocean Territory)
    case 0x13A:  // Diego Garcia
      return CountryCharsToCountryID('I', 'O');

    // Other cases where there is no ISO country code; we assign countries that
    // can serve as reasonable defaults.
    case 0x154:  // Rota Island
    case 0x155:  // Saipan
    case 0x15A:  // Tinian Island
      return CountryCharsToCountryID('U', 'S');
    case 0x134:  // Channel Islands
      return CountryCharsToCountryID('G', 'B');
    case 0x143:  // Guantanamo Bay
    default:
      return kCountryIDUnknown;
  }
}

int GetCurrentCountryID() {
  GEOID geo_id = GetUserGeoID(GEOCLASS_NATION);

  return GeoIDToCountryID(geo_id);
}

#elif defined(OS_MACOSX)

int GetCurrentCountryID() {
  base::mac::ScopedCFTypeRef<CFLocaleRef> locale(CFLocaleCopyCurrent());
  CFStringRef country = (CFStringRef)CFLocaleGetValue(locale.get(),
                                                      kCFLocaleCountryCode);
  if (!country)
    return kCountryIDUnknown;

  UniChar isobuf[2];
  CFRange char_range = CFRangeMake(0, 2);
  CFStringGetCharacters(country, char_range, isobuf);

  return CountryCharsToCountryIDWithUpdate(static_cast<char>(isobuf[0]),
                                           static_cast<char>(isobuf[1]));
}

#elif defined(OS_ANDROID)

// Initialized by InitCountryCode().
int g_country_code_at_install = kCountryIDNotSet;

int GetCurrentCountryID() {
  DCHECK(g_country_code_at_install != kCountryIDNotSet);
  return g_country_code_at_install;
}

#elif defined(OS_POSIX)

int GetCurrentCountryID() {
  const char* locale = setlocale(LC_MESSAGES, NULL);

  if (!locale)
    return kCountryIDUnknown;

  // The format of a locale name is:
  // language[_territory][.codeset][@modifier], where territory is an ISO 3166
  // country code, which is what we want.
  std::string locale_str(locale);
  size_t begin = locale_str.find('_');
  if (begin == std::string::npos || locale_str.size() - begin < 3)
    return kCountryIDUnknown;

  ++begin;
  size_t end = locale_str.find_first_of(".@", begin);
  if (end == std::string::npos)
    end = locale_str.size();

  // The territory part must contain exactly two characters.
  if (end - begin == 2) {
    return CountryCharsToCountryIDWithUpdate(
        base::ToUpperASCII(locale_str[begin]),
        base::ToUpperASCII(locale_str[begin + 1]));
  }

  return kCountryIDUnknown;
}

#endif  // OS_*

int GetCountryIDFromPrefs(PrefService* prefs) {
  // See if the user overrode the country on the command line.
  const std::string country(
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kCountry));
  if (country.length() == 2)
    return CountryCharsToCountryIDWithUpdate(country[0], country[1]);

  // Cache first run Country ID value in prefs, and use it afterwards.  This
  // ensures that just because the user moves around, we won't automatically
  // make major changes to their available search providers, which would feel
  // surprising.
  if (!prefs)
    return GetCurrentCountryID();

  int new_country_id = GetCurrentCountryID();
#if defined(OS_WIN)
  // Migrate the old platform-specific value if it's present.
  if (prefs->HasPrefPath(prefs::kGeoIDAtInstall)) {
    int geo_id = prefs->GetInteger(prefs::kGeoIDAtInstall);
    prefs->ClearPref(prefs::kGeoIDAtInstall);
    new_country_id = GeoIDToCountryID(geo_id);
  }
#endif

  if (!prefs->HasPrefPath(prefs::kCountryIDAtInstall))
    prefs->SetInteger(prefs::kCountryIDAtInstall, new_country_id);

  return prefs->GetInteger(prefs::kCountryIDAtInstall);
}

void GetPrepopulationSetFromCountryID(PrefService* prefs,
                                      const PrepopulatedEngine*** engines,
                                      size_t* num_engines) {
  // NOTE: This function should ALWAYS set its outparams.

  // If you add a new country make sure to update the unit test for coverage.
  switch (GetCountryIDFromPrefs(prefs)) {

#define CHAR_A 'A'
#define CHAR_B 'B'
#define CHAR_C 'C'
#define CHAR_D 'D'
#define CHAR_E 'E'
#define CHAR_F 'F'
#define CHAR_G 'G'
#define CHAR_H 'H'
#define CHAR_I 'I'
#define CHAR_J 'J'
#define CHAR_K 'K'
#define CHAR_L 'L'
#define CHAR_M 'M'
#define CHAR_N 'N'
#define CHAR_O 'O'
#define CHAR_P 'P'
#define CHAR_Q 'Q'
#define CHAR_R 'R'
#define CHAR_S 'S'
#define CHAR_T 'T'
#define CHAR_U 'U'
#define CHAR_V 'V'
#define CHAR_W 'W'
#define CHAR_X 'X'
#define CHAR_Y 'Y'
#define CHAR_Z 'Z'
#define CHAR(ch) CHAR_##ch
#define CODE_TO_ID(code1, code2)\
    (CHAR(code1) << 8 | CHAR(code2))

#define UNHANDLED_COUNTRY(code1, code2)\
    case CODE_TO_ID(code1, code2):
#define END_UNHANDLED_COUNTRIES(code1, code2)\
      *engines = engines_##code1##code2;\
      *num_engines = arraysize(engines_##code1##code2);\
      return;
#define DECLARE_COUNTRY(code1, code2)\
    UNHANDLED_COUNTRY(code1, code2)\
    END_UNHANDLED_COUNTRIES(code1, code2)

    // Countries with their own, dedicated engine set.
    DECLARE_COUNTRY(A, E)  // United Arab Emirates
    DECLARE_COUNTRY(A, L)  // Albania
    DECLARE_COUNTRY(A, R)  // Argentina
    DECLARE_COUNTRY(A, T)  // Austria
    DECLARE_COUNTRY(A, U)  // Australia
    DECLARE_COUNTRY(B, A)  // Bosnia and Herzegovina
    DECLARE_COUNTRY(B, E)  // Belgium
    DECLARE_COUNTRY(B, G)  // Bulgaria
    DECLARE_COUNTRY(B, H)  // Bahrain
    DECLARE_COUNTRY(B, I)  // Burundi
    DECLARE_COUNTRY(B, N)  // Brunei
    DECLARE_COUNTRY(B, O)  // Bolivia
    DECLARE_COUNTRY(B, R)  // Brazil
    DECLARE_COUNTRY(B, Y)  // Belarus
    DECLARE_COUNTRY(B, Z)  // Belize
    DECLARE_COUNTRY(C, A)  // Canada
    DECLARE_COUNTRY(C, H)  // Switzerland
    DECLARE_COUNTRY(C, L)  // Chile
    DECLARE_COUNTRY(C, N)  // China
    DECLARE_COUNTRY(C, O)  // Colombia
    DECLARE_COUNTRY(C, R)  // Costa Rica
    DECLARE_COUNTRY(C, Z)  // Czech Republic
    DECLARE_COUNTRY(D, E)  // Germany
    DECLARE_COUNTRY(D, K)  // Denmark
    DECLARE_COUNTRY(D, O)  // Dominican Republic
    DECLARE_COUNTRY(D, Z)  // Algeria
    DECLARE_COUNTRY(E, C)  // Ecuador
    DECLARE_COUNTRY(E, E)  // Estonia
    DECLARE_COUNTRY(E, G)  // Egypt
    DECLARE_COUNTRY(E, S)  // Spain
    DECLARE_COUNTRY(F, I)  // Finland
    DECLARE_COUNTRY(F, O)  // Faroe Islands
    DECLARE_COUNTRY(F, R)  // France
    DECLARE_COUNTRY(G, B)  // United Kingdom
    DECLARE_COUNTRY(G, R)  // Greece
    DECLARE_COUNTRY(G, T)  // Guatemala
    DECLARE_COUNTRY(H, K)  // Hong Kong
    DECLARE_COUNTRY(H, N)  // Honduras
    DECLARE_COUNTRY(H, R)  // Croatia
    DECLARE_COUNTRY(H, U)  // Hungary
    DECLARE_COUNTRY(I, D)  // Indonesia
    DECLARE_COUNTRY(I, E)  // Ireland
    DECLARE_COUNTRY(I, L)  // Israel
    DECLARE_COUNTRY(I, N)  // India
    DECLARE_COUNTRY(I, Q)  // Iraq
    DECLARE_COUNTRY(I, R)  // Iran
    DECLARE_COUNTRY(I, S)  // Iceland
    DECLARE_COUNTRY(I, T)  // Italy
    DECLARE_COUNTRY(J, M)  // Jamaica
    DECLARE_COUNTRY(J, O)  // Jordan
    DECLARE_COUNTRY(J, P)  // Japan
    DECLARE_COUNTRY(K, E)  // Kenya
    DECLARE_COUNTRY(K, R)  // South Korea
    DECLARE_COUNTRY(K, W)  // Kuwait
    DECLARE_COUNTRY(K, Z)  // Kazakhstan
    DECLARE_COUNTRY(L, B)  // Lebanon
    DECLARE_COUNTRY(L, I)  // Liechtenstein
    DECLARE_COUNTRY(L, T)  // Lithuania
    DECLARE_COUNTRY(L, U)  // Luxembourg
    DECLARE_COUNTRY(L, V)  // Latvia
    DECLARE_COUNTRY(L, Y)  // Libya
    DECLARE_COUNTRY(M, A)  // Morocco
    DECLARE_COUNTRY(M, C)  // Monaco
    DECLARE_COUNTRY(M, D)  // Moldova
    DECLARE_COUNTRY(M, E)  // Montenegro
    DECLARE_COUNTRY(M, K)  // Macedonia
    DECLARE_COUNTRY(M, X)  // Mexico
    DECLARE_COUNTRY(M, Y)  // Malaysia
    DECLARE_COUNTRY(N, I)  // Nicaragua
    DECLARE_COUNTRY(N, L)  // Netherlands
    DECLARE_COUNTRY(N, O)  // Norway
    DECLARE_COUNTRY(N, Z)  // New Zealand
    DECLARE_COUNTRY(O, M)  // Oman
    DECLARE_COUNTRY(P, A)  // Panama
    DECLARE_COUNTRY(P, E)  // Peru
    DECLARE_COUNTRY(P, H)  // Philippines
    DECLARE_COUNTRY(P, K)  // Pakistan
    DECLARE_COUNTRY(P, L)  // Poland
    DECLARE_COUNTRY(P, R)  // Puerto Rico
    DECLARE_COUNTRY(P, T)  // Portugal
    DECLARE_COUNTRY(P, Y)  // Paraguay
    DECLARE_COUNTRY(Q, A)  // Qatar
    DECLARE_COUNTRY(R, O)  // Romania
    DECLARE_COUNTRY(R, S)  // Serbia
    DECLARE_COUNTRY(R, U)  // Russia
    DECLARE_COUNTRY(R, W)  // Rwanda
    DECLARE_COUNTRY(S, A)  // Saudi Arabia
    DECLARE_COUNTRY(S, E)  // Sweden
    DECLARE_COUNTRY(S, G)  // Singapore
    DECLARE_COUNTRY(S, I)  // Slovenia
    DECLARE_COUNTRY(S, K)  // Slovakia
    DECLARE_COUNTRY(S, V)  // El Salvador
    DECLARE_COUNTRY(S, Y)  // Syria
    DECLARE_COUNTRY(T, H)  // Thailand
    DECLARE_COUNTRY(T, N)  // Tunisia
    DECLARE_COUNTRY(T, R)  // Turkey
    DECLARE_COUNTRY(T, T)  // Trinidad and Tobago
    DECLARE_COUNTRY(T, W)  // Taiwan
    DECLARE_COUNTRY(T, Z)  // Tanzania
    DECLARE_COUNTRY(U, A)  // Ukraine
    DECLARE_COUNTRY(U, S)  // United States
    DECLARE_COUNTRY(U, Y)  // Uruguay
    DECLARE_COUNTRY(V, E)  // Venezuela
    DECLARE_COUNTRY(V, N)  // Vietnam
    DECLARE_COUNTRY(Y, E)  // Yemen
    DECLARE_COUNTRY(Z, A)  // South Africa
    DECLARE_COUNTRY(Z, W)  // Zimbabwe

    // Countries using the "Australia" engine set.
    UNHANDLED_COUNTRY(C, C)  // Cocos Islands
    UNHANDLED_COUNTRY(C, X)  // Christmas Island
    UNHANDLED_COUNTRY(H, M)  // Heard Island and McDonald Islands
    UNHANDLED_COUNTRY(N, F)  // Norfolk Island
    END_UNHANDLED_COUNTRIES(A, U)

    // Countries using the "China" engine set.
    UNHANDLED_COUNTRY(M, O)  // Macao
    END_UNHANDLED_COUNTRIES(C, N)

    // Countries using the "Denmark" engine set.
    UNHANDLED_COUNTRY(G, L)  // Greenland
    END_UNHANDLED_COUNTRIES(D, K)

    // Countries using the "Spain" engine set.
    UNHANDLED_COUNTRY(A, D)  // Andorra
    END_UNHANDLED_COUNTRIES(E, S)

    // Countries using the "Finland" engine set.
    UNHANDLED_COUNTRY(A, X)  // Aland Islands
    END_UNHANDLED_COUNTRIES(F, I)

    // Countries using the "France" engine set.
    UNHANDLED_COUNTRY(B, F)  // Burkina Faso
    UNHANDLED_COUNTRY(B, J)  // Benin
    UNHANDLED_COUNTRY(C, D)  // Congo - Kinshasa
    UNHANDLED_COUNTRY(C, F)  // Central African Republic
    UNHANDLED_COUNTRY(C, G)  // Congo - Brazzaville
    UNHANDLED_COUNTRY(C, I)  // Ivory Coast
    UNHANDLED_COUNTRY(C, M)  // Cameroon
    UNHANDLED_COUNTRY(D, J)  // Djibouti
    UNHANDLED_COUNTRY(G, A)  // Gabon
    UNHANDLED_COUNTRY(G, F)  // French Guiana
    UNHANDLED_COUNTRY(G, N)  // Guinea
    UNHANDLED_COUNTRY(G, P)  // Guadeloupe
    UNHANDLED_COUNTRY(H, T)  // Haiti
#if defined(OS_WIN)
    UNHANDLED_COUNTRY(I, P)  // Clipperton Island ('IP' is an WinXP-ism; ISO
                             //                    includes it with France)
#endif
    UNHANDLED_COUNTRY(M, L)  // Mali
    UNHANDLED_COUNTRY(M, Q)  // Martinique
    UNHANDLED_COUNTRY(N, C)  // New Caledonia
    UNHANDLED_COUNTRY(N, E)  // Niger
    UNHANDLED_COUNTRY(P, F)  // French Polynesia
    UNHANDLED_COUNTRY(P, M)  // Saint Pierre and Miquelon
    UNHANDLED_COUNTRY(R, E)  // Reunion
    UNHANDLED_COUNTRY(S, N)  // Senegal
    UNHANDLED_COUNTRY(T, D)  // Chad
    UNHANDLED_COUNTRY(T, F)  // French Southern Territories
    UNHANDLED_COUNTRY(T, G)  // Togo
    UNHANDLED_COUNTRY(W, F)  // Wallis and Futuna
    UNHANDLED_COUNTRY(Y, T)  // Mayotte
    END_UNHANDLED_COUNTRIES(F, R)

    // Countries using the "Greece" engine set.
    UNHANDLED_COUNTRY(C, Y)  // Cyprus
    END_UNHANDLED_COUNTRIES(G, R)

    // Countries using the "Italy" engine set.
    UNHANDLED_COUNTRY(S, M)  // San Marino
    UNHANDLED_COUNTRY(V, A)  // Vatican
    END_UNHANDLED_COUNTRIES(I, T)

    // Countries using the "Morocco" engine set.
    UNHANDLED_COUNTRY(E, H)  // Western Sahara
    END_UNHANDLED_COUNTRIES(M, A)

    // Countries using the "Netherlands" engine set.
    UNHANDLED_COUNTRY(A, N)  // Netherlands Antilles
    UNHANDLED_COUNTRY(A, W)  // Aruba
    END_UNHANDLED_COUNTRIES(N, L)

    // Countries using the "Norway" engine set.
    UNHANDLED_COUNTRY(B, V)  // Bouvet Island
    UNHANDLED_COUNTRY(S, J)  // Svalbard and Jan Mayen
    END_UNHANDLED_COUNTRIES(N, O)

    // Countries using the "New Zealand" engine set.
    UNHANDLED_COUNTRY(C, K)  // Cook Islands
    UNHANDLED_COUNTRY(N, U)  // Niue
    UNHANDLED_COUNTRY(T, K)  // Tokelau
    END_UNHANDLED_COUNTRIES(N, Z)

    // Countries using the "Portugal" engine set.
    UNHANDLED_COUNTRY(C, V)  // Cape Verde
    UNHANDLED_COUNTRY(G, W)  // Guinea-Bissau
    UNHANDLED_COUNTRY(M, Z)  // Mozambique
    UNHANDLED_COUNTRY(S, T)  // Sao Tome and Principe
    UNHANDLED_COUNTRY(T, L)  // Timor-Leste
    END_UNHANDLED_COUNTRIES(P, T)

    // Countries using the "Russia" engine set.
    UNHANDLED_COUNTRY(A, M)  // Armenia
    UNHANDLED_COUNTRY(A, Z)  // Azerbaijan
    UNHANDLED_COUNTRY(K, G)  // Kyrgyzstan
    UNHANDLED_COUNTRY(T, J)  // Tajikistan
    UNHANDLED_COUNTRY(T, M)  // Turkmenistan
    UNHANDLED_COUNTRY(U, Z)  // Uzbekistan
    END_UNHANDLED_COUNTRIES(R, U)

    // Countries using the "Saudi Arabia" engine set.
    UNHANDLED_COUNTRY(M, R)  // Mauritania
    UNHANDLED_COUNTRY(P, S)  // Palestinian Territory
    UNHANDLED_COUNTRY(S, D)  // Sudan
    END_UNHANDLED_COUNTRIES(S, A)

    // Countries using the "United Kingdom" engine set.
    UNHANDLED_COUNTRY(B, M)  // Bermuda
    UNHANDLED_COUNTRY(F, K)  // Falkland Islands
    UNHANDLED_COUNTRY(G, G)  // Guernsey
    UNHANDLED_COUNTRY(G, I)  // Gibraltar
    UNHANDLED_COUNTRY(G, S)  // South Georgia and the South Sandwich
                             //   Islands
    UNHANDLED_COUNTRY(I, M)  // Isle of Man
    UNHANDLED_COUNTRY(I, O)  // British Indian Ocean Territory
    UNHANDLED_COUNTRY(J, E)  // Jersey
    UNHANDLED_COUNTRY(K, Y)  // Cayman Islands
    UNHANDLED_COUNTRY(M, S)  // Montserrat
    UNHANDLED_COUNTRY(M, T)  // Malta
    UNHANDLED_COUNTRY(P, N)  // Pitcairn Islands
    UNHANDLED_COUNTRY(S, H)  // Saint Helena, Ascension Island, and Tristan da
                             //   Cunha
    UNHANDLED_COUNTRY(T, C)  // Turks and Caicos Islands
    UNHANDLED_COUNTRY(V, G)  // British Virgin Islands
    END_UNHANDLED_COUNTRIES(G, B)

    // Countries using the "United States" engine set.
    UNHANDLED_COUNTRY(A, S)  // American Samoa
    UNHANDLED_COUNTRY(G, U)  // Guam
    UNHANDLED_COUNTRY(M, P)  // Northern Mariana Islands
    UNHANDLED_COUNTRY(U, M)  // U.S. Minor Outlying Islands
    UNHANDLED_COUNTRY(V, I)  // U.S. Virgin Islands
    END_UNHANDLED_COUNTRIES(U, S)

    // Countries using the "default" engine set.
    UNHANDLED_COUNTRY(A, F)  // Afghanistan
    UNHANDLED_COUNTRY(A, G)  // Antigua and Barbuda
    UNHANDLED_COUNTRY(A, I)  // Anguilla
    UNHANDLED_COUNTRY(A, O)  // Angola
    UNHANDLED_COUNTRY(A, Q)  // Antarctica
    UNHANDLED_COUNTRY(B, B)  // Barbados
    UNHANDLED_COUNTRY(B, D)  // Bangladesh
    UNHANDLED_COUNTRY(B, S)  // Bahamas
    UNHANDLED_COUNTRY(B, T)  // Bhutan
    UNHANDLED_COUNTRY(B, W)  // Botswana
    UNHANDLED_COUNTRY(C, U)  // Cuba
    UNHANDLED_COUNTRY(D, M)  // Dominica
    UNHANDLED_COUNTRY(E, R)  // Eritrea
    UNHANDLED_COUNTRY(E, T)  // Ethiopia
    UNHANDLED_COUNTRY(F, J)  // Fiji
    UNHANDLED_COUNTRY(F, M)  // Micronesia
    UNHANDLED_COUNTRY(G, D)  // Grenada
    UNHANDLED_COUNTRY(G, E)  // Georgia
    UNHANDLED_COUNTRY(G, H)  // Ghana
    UNHANDLED_COUNTRY(G, M)  // Gambia
    UNHANDLED_COUNTRY(G, Q)  // Equatorial Guinea
    UNHANDLED_COUNTRY(G, Y)  // Guyana
    UNHANDLED_COUNTRY(K, H)  // Cambodia
    UNHANDLED_COUNTRY(K, I)  // Kiribati
    UNHANDLED_COUNTRY(K, M)  // Comoros
    UNHANDLED_COUNTRY(K, N)  // Saint Kitts and Nevis
    UNHANDLED_COUNTRY(K, P)  // North Korea
    UNHANDLED_COUNTRY(L, A)  // Laos
    UNHANDLED_COUNTRY(L, C)  // Saint Lucia
    UNHANDLED_COUNTRY(L, K)  // Sri Lanka
    UNHANDLED_COUNTRY(L, R)  // Liberia
    UNHANDLED_COUNTRY(L, S)  // Lesotho
    UNHANDLED_COUNTRY(M, G)  // Madagascar
    UNHANDLED_COUNTRY(M, H)  // Marshall Islands
    UNHANDLED_COUNTRY(M, M)  // Myanmar
    UNHANDLED_COUNTRY(M, N)  // Mongolia
    UNHANDLED_COUNTRY(M, U)  // Mauritius
    UNHANDLED_COUNTRY(M, V)  // Maldives
    UNHANDLED_COUNTRY(M, W)  // Malawi
    UNHANDLED_COUNTRY(N, A)  // Namibia
    UNHANDLED_COUNTRY(N, G)  // Nigeria
    UNHANDLED_COUNTRY(N, P)  // Nepal
    UNHANDLED_COUNTRY(N, R)  // Nauru
    UNHANDLED_COUNTRY(P, G)  // Papua New Guinea
    UNHANDLED_COUNTRY(P, W)  // Palau
    UNHANDLED_COUNTRY(S, B)  // Solomon Islands
    UNHANDLED_COUNTRY(S, C)  // Seychelles
    UNHANDLED_COUNTRY(S, L)  // Sierra Leone
    UNHANDLED_COUNTRY(S, O)  // Somalia
    UNHANDLED_COUNTRY(S, R)  // Suriname
    UNHANDLED_COUNTRY(S, Z)  // Swaziland
    UNHANDLED_COUNTRY(T, O)  // Tonga
    UNHANDLED_COUNTRY(T, V)  // Tuvalu
    UNHANDLED_COUNTRY(U, G)  // Uganda
    UNHANDLED_COUNTRY(V, C)  // Saint Vincent and the Grenadines
    UNHANDLED_COUNTRY(V, U)  // Vanuatu
    UNHANDLED_COUNTRY(W, S)  // Samoa
    UNHANDLED_COUNTRY(Z, M)  // Zambia
    case kCountryIDUnknown:
    default:                // Unhandled location
    END_UNHANDLED_COUNTRIES(def, ault)
  }
}

}  // namespace

namespace TemplateURLPrepopulateData {

// The following id is for UMA stats only. Please update
// kMaxPrepopulatedEngineID if it changes upwards.
const int kMaxPrepopulatedEngineID = 101;

void RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterIntegerPref(prefs::kCountryIDAtInstall,
                             kCountryIDUnknown,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterListPref(prefs::kSearchProviderOverrides,
                          PrefService::UNSYNCABLE_PREF);
  prefs->RegisterIntegerPref(prefs::kSearchProviderOverridesVersion,
                             -1,
                             PrefService::UNSYNCABLE_PREF);
  // Obsolete pref, for migration.
  prefs->RegisterIntegerPref(prefs::kGeoIDAtInstall,
                             -1,
                             PrefService::UNSYNCABLE_PREF);
}

int GetDataVersion(PrefService* prefs) {
  // Increment this if you change the above data in ways that mean users with
  // existing data should get a new version.
  const int kCurrentDataVersion = 40;
  // Allow tests to override the local version.
  return (prefs && prefs->HasPrefPath(prefs::kSearchProviderOverridesVersion)) ?
      prefs->GetInteger(prefs::kSearchProviderOverridesVersion) :
      kCurrentDataVersion;
}

TemplateURL* MakePrepopulatedTemplateURL(Profile* profile,
                                         const string16& name,
                                         const string16& keyword,
                                         const base::StringPiece& search_url,
                                         const base::StringPiece& suggest_url,
                                         const base::StringPiece& instant_url,
                                         const base::StringPiece& favicon_url,
                                         const base::StringPiece& encoding,
                                         int id) {
  TemplateURLData data;
  data.short_name = name;
  data.SetKeyword(keyword);
  data.SetURL(search_url.as_string());
  data.suggestions_url = suggest_url.as_string();
  data.instant_url = instant_url.as_string();
  data.favicon_url = GURL(favicon_url.as_string());
  data.show_in_default_list = true;
  data.safe_for_autoreplace = true;
  data.input_encodings.push_back(encoding.as_string());
  data.date_created = base::Time();
  data.last_modified = base::Time();
  data.prepopulate_id = id;
  return new TemplateURL(profile, data);
}

void GetPrepopulatedTemplateFromPrefs(Profile* profile,
                                      std::vector<TemplateURL*>* t_urls) {
  if (!profile)
    return;

  const ListValue* list =
      profile->GetPrefs()->GetList(prefs::kSearchProviderOverrides);
  if (!list)
    return;

  string16 name;
  string16 keyword;
  std::string search_url;
  std::string suggest_url;
  std::string instant_url;
  std::string favicon_url;
  std::string encoding;
  int id;

  size_t num_engines = list->GetSize();
  for (size_t i = 0; i != num_engines; ++i) {
    const Value* val;
    const DictionaryValue* engine;
    if (list->GetDictionary(i, &engine) &&
        engine->Get("name", &val) && val->GetAsString(&name) &&
        engine->Get("keyword", &val) && val->GetAsString(&keyword) &&
        engine->Get("search_url", &val) && val->GetAsString(&search_url) &&
        engine->Get("suggest_url", &val) && val->GetAsString(&suggest_url) &&
        engine->Get("instant_url", &val) && val->GetAsString(&instant_url) &&
        engine->Get("favicon_url", &val) && val->GetAsString(&favicon_url) &&
        engine->Get("encoding", &val) && val->GetAsString(&encoding) &&
        engine->Get("id", &val) && val->GetAsInteger(&id)) {
      // These next fields are not allowed to be empty.
      if (name.empty() || keyword.empty() || search_url.empty() ||
          favicon_url.empty() || encoding.empty())
        return;
    } else {
      // Got a parsing error. No big deal.
      continue;
    }
    t_urls->push_back(MakePrepopulatedTemplateURL(profile, name, keyword,
        search_url, suggest_url, instant_url, favicon_url, encoding, id));
  }
}

// The caller owns the returned TemplateURL.
TemplateURL* MakePrepopulatedTemplateURLFromPrepopulateEngine(
    Profile* profile,
    const PrepopulatedEngine& engine) {
  return MakePrepopulatedTemplateURL(profile, WideToUTF16(engine.name),
      WideToUTF16(engine.keyword), engine.search_url, engine.suggest_url,
      engine.instant_url, engine.favicon_url, engine.encoding, engine.id);
}

void GetPrepopulatedEngines(Profile* profile,
                            std::vector<TemplateURL*>* t_urls,
                            size_t* default_search_provider_index) {
  // If there is a set of search engines in the preferences file, it overrides
  // the built-in set.
  *default_search_provider_index = 0;
  GetPrepopulatedTemplateFromPrefs(profile, t_urls);
  if (!t_urls->empty())
    return;

  const PrepopulatedEngine** engines;
  size_t num_engines;
  GetPrepopulationSetFromCountryID(profile ? profile->GetPrefs() : NULL,
                                   &engines, &num_engines);
  for (size_t i = 0; i != num_engines; ++i) {
    t_urls->push_back(
        MakePrepopulatedTemplateURLFromPrepopulateEngine(profile, *engines[i]));
  }
}

TemplateURL* GetPrepopulatedDefaultSearch(Profile* profile) {
  TemplateURL* default_search_provider = NULL;
  ScopedVector<TemplateURL> loaded_urls;
  size_t default_search_index;
  // This could be more efficient.  We are loading all the URLs to only keep
  // the first one.
  GetPrepopulatedEngines(profile, &loaded_urls.get(), &default_search_index);
  if (default_search_index < loaded_urls.size()) {
    default_search_provider = loaded_urls[default_search_index];
    loaded_urls.weak_erase(loaded_urls.begin() + default_search_index);
  }
  return default_search_provider;
}

SearchEngineType GetEngineType(const std::string& url) {
  // Restricted to UI thread because ReplaceSearchTerms() is so restricted.
  using content::BrowserThread;
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));

  // We may get a valid URL, or we may get the Google prepopulate URL which
  // can't be converted directly to a GURL.  To handle the latter, we first
  // construct a TemplateURL from the provided |url|, then call
  // ReplaceSearchTerms().  This should return a valid URL even when the input
  // has Google base URLs.
  TemplateURLData data;
  data.SetURL(url);
  TemplateURL turl(NULL, data);
  GURL as_gurl(turl.url_ref().ReplaceSearchTerms(
      TemplateURLRef::SearchTermsArgs(ASCIIToUTF16("x"))));
  if (!as_gurl.is_valid())
    return SEARCH_ENGINE_OTHER;

  // Check using origins, in order to more aggressively match search engine
  // types for data imported from other browsers.
  //
  // First special-case Google, because the prepopulate URL for it will not
  // convert to a GURL and thus won't have an origin.  Instead see if the
  // incoming URL's host is "[*.]google.<TLD>".
  if (google_util::IsGoogleHostname(as_gurl.host(),
                                    google_util::DISALLOW_SUBDOMAIN))
    return google.type;

  // Now check the rest of the prepopulate data.
  GURL origin(as_gurl.GetOrigin());
  for (size_t i = 0; i < arraysize(kAllEngines); ++i) {
    GURL engine_url(kAllEngines[i]->search_url);
    if (engine_url.is_valid() && (origin == engine_url.GetOrigin()))
      return kAllEngines[i]->type;
  }

  return SEARCH_ENGINE_OTHER;
}

#if defined(OS_ANDROID)

void InitCountryCode(const std::string& country_code) {
  if (country_code.size() != 2) {
    DLOG(ERROR) << "Invalid country code: " << country_code;
    g_country_code_at_install = kCountryIDUnknown;
  } else {
    g_country_code_at_install =
        CountryCharsToCountryIDWithUpdate(country_code[0], country_code[1]);
  }
}

#endif

}  // namespace TemplateURLPrepopulateData
