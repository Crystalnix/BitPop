#!/usr/bin/python
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import hashlib
import math
import optparse
import re
import subprocess
import sys
import time
import urllib2


try:
  import json
except ImportError:
  import simplejson as json


__version__ = '1.0'
DEFAULT_EXPECTATIONS_FILE = 'perf_expectations.json'
DEFAULT_VARIANCE = 0.05
USAGE = ''


def ReadFile(filename):
  try:
    file = open(filename, 'r')
  except IOError, e:
    print >> sys.stderr, ('I/O Error reading file %s(%s): %s' %
                          (filename, e.errno, e.strerror))
    raise e
  contents = file.read()
  file.close()
  return contents


def ConvertJsonIntoDict(string):
  """Read a JSON string and convert its contents into a Python datatype."""
  if len(string) == 0:
    print >> sys.stderr, ('Error could not parse empty string')
    raise Exception('JSON data missing')

  try:
    jsondata = json.loads(string)
  except ValueError, e:
    print >> sys.stderr, ('Error parsing string: "%s"' % string)
    raise e
  return jsondata


# Floating point representation of last time we fetched a URL.
last_fetched_at = None
def FetchUrlContents(url):
  global last_fetched_at
  if last_fetched_at and ((time.time() - last_fetched_at) <= 0.5):
    # Sleep for half a second to avoid overloading the server.
    time.sleep(0.5)
  try:
    last_fetched_at = time.time()
    connection = urllib2.urlopen(url)
  except urllib2.HTTPError, e:
    if e.code == 404:
      return None
    raise e
  text = connection.read().strip()
  connection.close()
  return text


def GetRowData(data, key):
  rowdata = []
  # reva and revb always come first.
  for subkey in ['reva', 'revb']:
    if subkey in data[key]:
      rowdata.append('"%s": %s' % (subkey, data[key][subkey]))
  # Strings, like type, come next.
  for subkey in ['type']:
    if subkey in data[key]:
      rowdata.append('"%s": "%s"' % (subkey, data[key][subkey]))
  # Finally improve/regress numbers come last.
  for subkey in ['improve', 'regress']:
    if subkey in data[key]:
      rowdata.append('"%s": %s' % (subkey, data[key][subkey]))
  return rowdata


def GetRowDigest(rowdata, key):
  sha1 = hashlib.sha1()
  rowdata = [str(possibly_unicode_string).encode('ascii')
             for possibly_unicode_string in rowdata]
  sha1.update(str(rowdata) + key)
  return sha1.hexdigest()[0:8]


def WriteJson(filename, data, keys):
  """Write a list of |keys| in |data| to the file specified in |filename|."""
  try:
    file = open(filename, 'w')
  except IOError, e:
    print >> sys.stderr, ('I/O Error writing file %s(%s): %s' %
                          (filename, e.errno, e.strerror))
    return False
  jsondata = []
  for key in keys:
    rowdata = GetRowData(data, key)
    # Include an updated checksum.
    rowdata.append('"sha1": "%s"' % GetRowDigest(rowdata, key))
    jsondata.append('"%s": {%s}' % (key, ', '.join(rowdata)))
  jsondata.append('"load": true')
  jsontext = '{%s\n}' % ',\n '.join(jsondata)
  file.write(jsontext + '\n')
  file.close()
  return True


last_key_printed = None
def Main(args):
  def OutputMessage(message, verbose_message=True):
    global last_key_printed
    if not options.verbose and verbose_message:
      return

    if key != last_key_printed:
      last_key_printed = key
      print '\n' + key + ':'
    print '  %s' % message

  parser = optparse.OptionParser(usage=USAGE, version=__version__)
  parser.add_option('-v', '--verbose', action='store_true', default=False,
                    help='enable verbose output')
  options, args = parser.parse_args(args)

  if options.verbose:
    print 'Verbose output enabled.'

  # Get the list of summaries for a test.
  base_url = 'http://build.chromium.org/f/chromium/perf'
  perf = ConvertJsonIntoDict(ReadFile(DEFAULT_EXPECTATIONS_FILE))

  # Fetch graphs.dat for this combination.
  perfkeys = perf.keys()
  # In perf_expectations.json, ignore the 'load' key.
  perfkeys.remove('load')
  perfkeys.sort()

  write_new_expectations = False
  for key in perfkeys:
    value = perf[key]
    variance = DEFAULT_VARIANCE

    # Verify the checksum.
    original_checksum = value.get('sha1', '')
    if 'sha1' in value:
      del value['sha1']
    rowdata = GetRowData(perf, key)
    computed_checksum = GetRowDigest(rowdata, key)
    if original_checksum == computed_checksum:
      OutputMessage('checksum matches, skipping')
      continue

    # Skip expectations that are missing a reva or revb.  We can't generate
    # expectations for those.
    if not(value.has_key('reva') and value.has_key('revb')):
      OutputMessage('missing revision range, skipping')
      continue
    revb = int(value['revb'])
    reva = int(value['reva'])

    # Ensure that reva is less than revb.
    if reva > revb:
      temp = reva
      reva = revb
      revb = temp

    # Get the system/test/graph/tracename and reftracename for the current key.
    matchData = re.match(r'^([^/]+)\/([^/]+)\/([^/]+)\/([^/]+)$', key)
    if not matchData:
      OutputMessage('cannot parse key, skipping')
      continue
    system = matchData.group(1)
    test = matchData.group(2)
    graph = matchData.group(3)
    tracename = matchData.group(4)
    reftracename = tracename + '_ref'

    # Create the summary_url and get the json data for that URL.
    # FetchUrlContents() may sleep to avoid overloading the server with
    # requests.
    summary_url = '%s/%s/%s/%s-summary.dat' % (base_url, system, test, graph)
    summaryjson = FetchUrlContents(summary_url)
    if not summaryjson:
      OutputMessage('missing json data, skipping')
      continue

    # Set value's type to 'relative' by default.
    value_type = value.get('type', 'relative')

    summarylist = summaryjson.split('\n')
    trace_values = {}
    traces = [tracename]
    if value_type == 'relative':
      traces += [reftracename]
    for trace in traces:
      trace_values.setdefault(trace, {})

    # Find the high and low values for each of the traces.
    scanning = False
    for line in summarylist:
      jsondata = ConvertJsonIntoDict(line)
      if int(jsondata['rev']) <= revb:
        scanning = True
      if int(jsondata['rev']) < reva:
        break

      # We found the upper revision in the range.  Scan for trace data until we
      # find the lower revision in the range.
      if scanning:
        for trace in traces:
          if trace not in jsondata['traces']:
            OutputMessage('trace %s missing' % trace)
            continue
          if type(jsondata['traces'][trace]) != type([]):
            OutputMessage('trace %s format not recognized' % trace)
            continue
          try:
            tracevalue = float(jsondata['traces'][trace][0])
          except ValueError:
            OutputMessage('trace %s value error: %s' % (
                trace, str(jsondata['traces'][trace][0])))
            continue

          for bound in ['high', 'low']:
            trace_values[trace].setdefault(bound, tracevalue)

          trace_values[trace]['high'] = max(trace_values[trace]['high'],
                                            tracevalue)
          trace_values[trace]['low'] = min(trace_values[trace]['low'],
                                           tracevalue)

    if 'high' not in trace_values[tracename]:
      OutputMessage('no suitable traces matched, skipping')
      continue

    if value_type == 'relative':
      # Calculate assuming high deltas are regressions and low deltas are
      # improvements.
      regress = (float(trace_values[tracename]['high']) -
                 float(trace_values[reftracename]['low']))
      improve = (float(trace_values[tracename]['low']) -
                 float(trace_values[reftracename]['high']))
    elif value_type == 'absolute':
      # Calculate assuming high absolutes are regressions and low absolutes are
      # improvements.
      regress = float(trace_values[tracename]['high'])
      improve = float(trace_values[tracename]['low'])

    # If the existing values assume regressions are low deltas relative to
    # improvements, swap our regress and improve.  This value must be a
    # scores-like result.
    if ('regress' in perf[key] and 'improve' in perf[key] and
        perf[key]['regress'] < perf[key]['improve']):
      temp = regress
      regress = improve
      improve = temp
    if regress < improve:
      regress = int(math.floor(regress - abs(regress*variance)))
      improve = int(math.ceil(improve + abs(improve*variance)))
    else:
      improve = int(math.floor(improve - abs(improve*variance)))
      regress = int(math.ceil(regress + abs(regress*variance)))

    if ('regress' in perf[key] and 'improve' in perf[key] and
        perf[key]['regress'] == regress and perf[key]['improve'] == improve):
      OutputMessage('no change')
      continue

    write_new_expectations = True
    OutputMessage('traces: %s' % trace_values, verbose_message=False)
    OutputMessage('before: %s' % perf[key], verbose_message=False)
    perf[key]['regress'] = regress
    perf[key]['improve'] = improve
    OutputMessage('after: %s' % perf[key], verbose_message=False)

  if write_new_expectations:
    print '\nWriting expectations... ',
    WriteJson(DEFAULT_EXPECTATIONS_FILE, perf, perfkeys)
    print 'done'
  else:
    if options.verbose:
      print ''
    print 'No changes.'


if __name__ == '__main__':
  sys.exit(Main(sys.argv))
